// TouchEngine SDK integration. SDK copyright and terms: licenses/touchengine.txt.
#include "Tox.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>
#include <atomic>
#include <array>
#include <mutex>

#ifdef PINOL_HAS_TOUCHENGINE
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#endif
#include <TouchEngine/TEOpenGL.h>
#include <TouchEngine/TEObject.h>

namespace {
template<class T> struct Ref {
    T *p = nullptr;
    ~Ref() { TERelease(&p); }
    Ref() = default;
    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
};

// The SDK may change bindings. Restore the host's state, including the texture unit
// and raster state, even after a failed import or shader compilation.
struct GlState {
    GLint program, vao, readFbo, drawFbo, viewport[4], active, tex2d, texRect, sampler, unpackBuffer, unpackAlign, unpackRow;
    GLboolean mask[4];
    const std::array<GLenum, 7> caps{{GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST,
                                    GL_CULL_FACE, GL_RASTERIZER_DISCARD, GL_FRAMEBUFFER_SRGB}};
    std::array<GLboolean, 7> enabled;
    GlState() {
        auto *g = gl();
        g->glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        g->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        g->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        g->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        g->glGetIntegerv(GL_VIEWPORT, viewport);
        g->glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        g->glActiveTexture(GL_TEXTURE0);
        g->glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
        g->glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
        g->glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlign);
        g->glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpackRow);
        g->glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex2d);
        g->glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE, &texRect);
        g->glGetBooleanv(GL_COLOR_WRITEMASK, mask);
        for (size_t i = 0; i < caps.size(); ++i) enabled[i] = g->glIsEnabled(caps[i]);
    }
    ~GlState() {
        auto *g = gl();
        g->glUseProgram(program);
        g->glBindVertexArray(vao);
        g->glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
        g->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
        g->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        g->glActiveTexture(GL_TEXTURE0);
        g->glBindTexture(GL_TEXTURE_2D, tex2d);
        g->glBindTexture(GL_TEXTURE_RECTANGLE, texRect);
        g->glBindSampler(0, sampler);
        g->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
        g->glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlign);
        g->glPixelStorei(GL_UNPACK_ROW_LENGTH, unpackRow);
        g->glActiveTexture(active);
        g->glColorMask(mask[0], mask[1], mask[2], mask[3]);
        for (size_t i = 0; i < caps.size(); ++i)
            if (enabled[i]) g->glEnable(caps[i]); else g->glDisable(caps[i]);
    }
};

class NativeTox final : public ToxPlayer {
    struct Image { GLuint texture = 0; int w = 0, h = 0; };
    struct Slot {
        Texture2D image;
        TEOpenGLTexture *handle = nullptr;
        std::atomic<int> users{0};
        std::atomic<bool> released{false};
        static void event(GLuint, TEObjectEvent e, void *info) {
            auto &s = *static_cast<Slot *>(info);
            if (e == TEObjectEventBeginUse) ++s.users;
            if (e == TEObjectEventEndUse) --s.users;
            if (e == TEObjectEventRelease) s.released = true;
        }
        void destroy() { TERelease(&handle); image.destroy(); }
    };
    QString m_path;
    mutable std::mutex m_mutex;
    ToxSettings m_settings;
    QString m_error, m_status = QStringLiteral("Loading TouchEngine…");
    std::vector<ToxControl> m_controls;
    std::vector<std::pair<QString, QString>> m_inputs, m_outputs;
    QStringList m_pulses;
    std::atomic<quint64> m_revision{0};
    std::atomic<bool> m_loaded{false}, m_schema{false}, m_finished{false}, m_reload{false};
    bool m_started = false, m_resumed = false, m_cooking = false;
    TEInstance *m_instance = nullptr;
    TEOpenGLContext *m_context = nullptr;
    QMap<QString, Image> m_images;
    QMap<QString, std::array<std::shared_ptr<Slot>, 3>> m_pool;
    std::vector<std::shared_ptr<Slot>> m_retired;
    QElapsedTimer m_clock;
    double m_lastCook = -1, m_time = 0;
    GLuint m_program[2]{}, m_vao = 0, m_fbo = 0;
    // Keep a shared output locked until the GPU has finished copying it. No CPU
    // readback or glFinish in the render loop; the next update polls this fence.
    TEOpenGLTexture *m_output = nullptr;
    GLsync m_fence = nullptr;

    void message(const QString &s, bool fatal = false) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = s;
        if (fatal) m_error = s;
    }
    bool check(TEResult r) {
        if (TEResultGetSeverity(r) != TESeverityError) return true;
        message(QString::fromUtf8(TEResultGetDescription(r)), true);
        return false;
    }
    static void event(TEInstance *, TEEvent e, TEResult r, int64_t, int32_t, int64_t, int32_t, void *info) {
        auto &self = *static_cast<NativeTox *>(info);
        if (r != TEResultSuccess && r != TEResultCancelled) {
            const bool fatal = TEResultGetSeverity(r) == TESeverityError && r != TEResultComponentErrors;
            self.message(QString::fromUtf8(TEResultGetDescription(r)), fatal);
        }
        if (e == TEEventInstanceDidLoad && (r == TEResultSuccess || r == TEResultComponentErrors ||
                                          TEResultGetSeverity(r) == TESeverityWarning)) {
            self.m_loaded = true;
            self.m_schema = true;
        }
        if (e == TEEventFrameDidFinish) self.m_finished = true;
    }
    static void link(TEInstance *, TELinkEvent e, const char *, void *info) {
        if (e != TELinkEventValueChange) static_cast<NativeTox *>(info)->m_schema = true;
    }
    void releaseOutput(bool wait) {
        if (!m_output) return;
        if (m_fence) {
            auto *g = gl();
            const GLenum r = g->glClientWaitSync(m_fence, wait ? GL_SYNC_FLUSH_COMMANDS_BIT : 0,
                                                wait ? GL_TIMEOUT_IGNORED : 0);
            if (!wait && r != GL_ALREADY_SIGNALED && r != GL_CONDITION_SATISFIED) return;
            g->glDeleteSync(m_fence);
            m_fence = nullptr;
        }
#ifdef Q_OS_WIN
        TEOpenGLTextureUnlock(m_output);
#endif
        TERelease(&m_output);
    }
    void close() {
        releaseOutput(true);
        if (m_instance && m_cooking) TEInstanceCancelFrame(m_instance);
        TERelease(&m_instance); // joins callbacks before their owner/pool is destroyed
        for (auto &pool : m_pool) for (auto &slot : pool) if (slot) { TERelease(&slot->handle); m_retired.push_back(slot); }
        m_pool.clear();
        for (auto &slot : m_retired) slot->image.destroy();
        m_retired.clear();
        TERelease(&m_context);
        m_cooking = m_resumed = m_started = false;
        m_loaded = m_finished = false;
        m_lastCook = -1; m_time = 0;
    }
    bool open() {
        m_started = true;
        if (!QFileInfo::exists(m_path)) { message(QStringLiteral("TouchDesigner component not found: ") + m_path, true); return false; }
        if (!check(TEInstanceCreate(event, link, this, &m_instance))) return false;
        const QByteArray enginePath = qgetenv("TOUCHENGINE_APP_PATH");
        if (!enginePath.isEmpty() && !check(TEInstanceSetPreferredEnginePath(m_instance, enginePath.constData()))) return false;
#ifdef Q_OS_WIN
        if (!check(TEOpenGLContextCreate(wglGetCurrentDC(), wglGetCurrentContext(), &m_context))) return false;
#else
        if (!check(TEOpenGLContextCreate(CGLGetCurrentContext(), &m_context))) return false;
#endif
        if (!check(TEInstanceAssociateGraphicsContext(m_instance, reinterpret_cast<TEGraphicsContext *>(m_context)))) return false;
        if (!check(TEInstanceSetOutputTextureOrigin(m_instance, TETextureOriginBottomLeft))) return false;
        const QByteArray path = m_path.toUtf8();
        if (!check(TEInstanceConfigure(m_instance, path.constData(), TETimeExternal))) return false;
        m_clock.start();
        return check(TEInstanceLoad(m_instance));
    }
    void discover() {
        std::vector<ToxControl> controls;
        std::vector<std::pair<QString, QString>> inputs, outputs;
        QSet<QString> seen;
        std::function<void(const char *, int)> walk = [&](const char *parent, int depth) {
            if (depth > 16 || seen.size() >= 4096) return;
            Ref<TEStringArray> children;
            if (TEInstanceLinkGetChildren(m_instance, parent, &children.p) != TEResultSuccess || !children.p) return;
            for (int i = 0; i < children.p->count && seen.size() < 4096; ++i) {
                const char *id = children.p->strings[i];
                const QString key = QString::fromUtf8(id);
                if (seen.contains(key)) continue;
                seen.insert(key);
                Ref<TELinkInfo> info;
                if (TEInstanceLinkGetInfo(m_instance, id, &info.p) != TEResultSuccess || !info.p) continue;
                const auto &in = *info.p;
                const QString label = in.label && *in.label ? QString::fromUtf8(in.label) : key;
                if (in.type == TELinkTypeTexture) {
                    auto &tops = in.scope == TEScopeInput ? inputs : outputs;
                    if (tops.size() < 64) tops.emplace_back(key, label);
                } else if (in.scope == TEScopeInput && in.count > 0 && in.count <= 16) {
                    ToxControl c;
                    c.id = key; c.label = label; c.count = in.count;
                    c.color = in.intent == TELinkIntentColorRGBA;
                    Ref<TELinkState> state;
                    TEInstanceLinkGetState(m_instance, id, &state.p);
                    if (state.p && (!state.p->enabled || !state.p->editable)) continue;
                    if (in.type == TELinkTypeBoolean) {
                        bool value = false;
                        TEInstanceLinkGetBooleanValue(m_instance, id, TELinkValueCurrent, &value);
                        c.type = in.intent == TELinkIntentPulse || in.intent == TELinkIntentMomentary
                                     ? ParamInfo::Type::Trigger : ParamInfo::Type::Bool;
                        c.initial = value; c.count = 1;
                    } else if (in.type == TELinkTypeDouble || in.type == TELinkTypeInt) {
                        c.type = in.type == TELinkTypeInt ? ParamInfo::Type::Int : ParamInfo::Type::Float;
                        auto read = [&](TELinkValue which, double fallback) {
                            std::vector<double> v(size_t(c.count), fallback);
                            if (in.type == TELinkTypeDouble) TEInstanceLinkGetDoubleValue(m_instance, id, which, v.data(), c.count);
                            else {
                                std::vector<int32_t> ints(size_t(c.count), int32_t(fallback));
                                TEInstanceLinkGetIntValue(m_instance, id, which, ints.data(), c.count);
                                for (int k = 0; k < c.count; ++k) v[size_t(k)] = ints[size_t(k)];
                            }
                            for (int k = 0; k < c.count; ++k)
                                if (which != TELinkValueCurrent && !TEInstanceLinkHasValue(m_instance, id, which, k)) v[size_t(k)] = fallback;
                            return v;
                        };
                        QJsonArray a;
                        for (double x : read(TELinkValueCurrent, 0)) a.append(x);
                        c.initial = c.count == 1 ? a[0] : QJsonValue(a);
                        c.minimum = read(TELinkValueUIMinimum, c.color ? 0 : -10000);
                        c.maximum = read(TELinkValueUIMaximum, c.color ? 1 : 10000);
                    } else if (in.type == TELinkTypeString) {
                        Ref<TEString> s;
                        TEInstanceLinkGetStringValue(m_instance, id, TELinkValueCurrent, &s.p);
                        c.type = ParamInfo::Type::Text; c.count = 1;
                        c.initial = s.p ? QString::fromUtf8(s.p->string) : QString();
                    } else { walk(id, depth + 1); continue; }
                    controls.push_back(std::move(c));
                }
                if (in.type == TELinkTypeGroup || in.type == TELinkTypeComplex || in.type == TELinkTypeSequence) walk(id, depth + 1);
            }
        };
        walk(nullptr, 0);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_controls = std::move(controls); m_inputs = std::move(inputs); m_outputs = std::move(outputs);
        ++m_revision;
    }
    bool copy(GLuint source, GLenum target, int w, int h, Texture2D &dest,
              TETextureOrigin origin = TETextureOriginBottomLeft, TETextureComponentMap map = kTETextureComponentMapIdentity) {
        if (!source || w <= 0 || h <= 0 || w > 16384 || h > 16384) return false;
        const int rect = target == GL_TEXTURE_RECTANGLE ? 1 : 0;
        if (!rect && target != GL_TEXTURE_2D) { message(QStringLiteral("Unsupported TouchEngine texture target"), true); return false; }
        auto *g = gl();
        if (!m_program[rect]) {
            const QString vs = QStringLiteral("#version 330 core\nout vec2 uv; void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); uv=p; gl_Position=vec4(p*2.-1.,0,1);}");
            const QString fs = QStringLiteral("#version 330 core\nin vec2 uv; out vec4 frag; uniform %1 image; uniform vec2 size; uniform bool flipY; uniform ivec4 map; float channel(vec4 c,int i){if(i==0)return 0.; if(i==1)return 1.;return c[i-2];} void main(){vec2 p=vec2(uv.x,flipY?1.-uv.y:uv.y);vec4 c=texture(image,%2);frag=vec4(channel(c,map.x),channel(c,map.y),channel(c,map.z),channel(c,map.w));}")
                                   .arg(rect ? "sampler2DRect" : "sampler2D", rect ? "p*size" : "p");
            QString error;
            m_program[rect] = compileProgram(vs, fs, &error);
            if (!m_program[rect]) { message(error, true); return false; }
        }
        const GLenum format = renderBits() > 8 ? GL_RGBA32F : GL_RGBA8;
        g->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        if (!dest.tex || dest.w != w || dest.h != h || dest.storageFormat != format) {
            if (renderBits() > 8) dest.uploadFloat(nullptr, w, h); else dest.upload(nullptr, w, h);
        }
        if (!m_vao) g->glGenVertexArrays(1, &m_vao);
        if (!m_fbo) g->glGenFramebuffers(1, &m_fbo);
        g->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo);
        g->glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dest.tex, 0);
        if (g->glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { message(QStringLiteral("Cannot allocate TouchEngine texture"), true); return false; }
        for (GLenum cap : {GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_CULL_FACE, GL_RASTERIZER_DISCARD, GL_FRAMEBUFFER_SRGB}) g->glDisable(cap);
        g->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        g->glViewport(0, 0, w, h);
        const GLuint p = m_program[rect];
        g->glUseProgram(p);
        g->glActiveTexture(GL_TEXTURE0);
        g->glBindTexture(target, source);
        g->glBindSampler(0, 0);
        g->glUniform1i(g->glGetUniformLocation(p, "image"), 0);
        g->glUniform2f(g->glGetUniformLocation(p, "size"), float(w), float(h));
        g->glUniform1i(g->glGetUniformLocation(p, "flipY"), origin == TETextureOriginTopLeft);
        g->glUniform4i(g->glGetUniformLocation(p, "map"), map.r, map.g, map.b, map.a);
        g->glBindVertexArray(m_vao);
        g->glDrawArrays(GL_TRIANGLES, 0, 3);
        return true;
    }
    void sendInputs(const ToxSettings &s) {
        for (const auto &top : inputs()) {
            const QByteArray id = top.first.toUtf8();
            const Image img = m_images.value(top.first);
            if (!s.inputs.value(top.first) || !img.texture) {
                check(TEInstanceLinkSetTextureValue(m_instance, id.constData(), nullptr, reinterpret_cast<TEGraphicsContext *>(m_context)));
                continue;
            }
            auto &pool = m_pool[top.first];
            for (auto &entry : pool) {
                if (!entry) entry = std::make_shared<Slot>();
                if (entry->users.load() != 0) continue;
                if (m_retired.size() >= 192) { message(QStringLiteral("TouchEngine is retaining resized input textures; reload the component"), true); break; }
                if (entry->handle && (entry->image.w != img.w || entry->image.h != img.h ||
                                     entry->image.storageFormat != (renderBits() > 8 ? GL_RGBA32F : GL_RGBA8))) {
                    // A texture must survive until the SDK's final Release, even after
                    // EndUse. Retire resized slots; reclaim them only on the GL thread.
                    TERelease(&entry->handle);
                    m_retired.push_back(entry);
                    entry = std::make_shared<Slot>();
                }
                Slot &slot = *entry;
                if (!copy(img.texture, GL_TEXTURE_2D, img.w, img.h, slot.image)) break;
                if (!slot.handle) slot.handle = TEOpenGLTextureCreate(slot.image.tex, GL_TEXTURE_2D, slot.image.storageFormat,
                         img.w, img.h, TETextureOriginBottomLeft, kTETextureComponentMapIdentity, Slot::event, &slot);
                if (slot.handle) check(TEInstanceLinkSetTextureValue(m_instance, id.constData(), reinterpret_cast<TETexture *>(slot.handle), reinterpret_cast<TEGraphicsContext *>(m_context)));
                break;
            }
        }
    }
    bool receive(Texture2D &dest, const ToxSettings &s) {
        const auto tops = outputs();
        QString selected = s.output;
        if (selected.isEmpty() && !tops.empty()) selected = tops.front().first;
        if (selected.isEmpty()) { message(QStringLiteral("The component exposes no output TOP"), true); return false; }
        if (std::none_of(tops.begin(), tops.end(), [&](const auto &p) { return p.first == selected; })) {
            message(QStringLiteral("Output TOP is no longer available: ") + selected, true); return false;
        }
        Ref<TETexture> value;
        const QByteArray id = selected.toUtf8();
        if (!check(TEInstanceLinkGetTextureValue(m_instance, id.constData(), TELinkValueCurrent, &value.p)) || !value.p) return false;
        TEOpenGLTexture *texture = nullptr;
        const auto type = TETextureGetType(value.p);
        if (type == TETextureTypeOpenGL) texture = reinterpret_cast<TEOpenGLTexture *>(TERetain(value.p));
#ifdef Q_OS_WIN
        else if (type == TETextureTypeD3DShared) check(TEOpenGLContextGetTexture(m_context, reinterpret_cast<TED3DSharedTexture *>(value.p), &texture));
#else
        else if (type == TETextureTypeIOSurface) check(TEOpenGLContextGetTexture(m_context, reinterpret_cast<TEIOSurfaceTexture *>(value.p), &texture));
#endif
        if (!texture) { message(QStringLiteral("Cannot import the TouchEngine output texture"), true); return false; }
#ifdef Q_OS_WIN
        if (!check(TEOpenGLTextureLock(texture))) { TERelease(&texture); return false; }
#endif
        const bool copied = copy(TEOpenGLTextureGetName(texture), TEOpenGLTextureGetTarget(texture),
                                 TEOpenGLTextureGetWidth(texture), TEOpenGLTextureGetHeight(texture), dest,
                                 TETextureGetOrigin(reinterpret_cast<TETexture *>(texture)),
                                 TETextureGetComponentMap(reinterpret_cast<TETexture *>(texture)));
        if (copied) { std::lock_guard<std::mutex> lock(m_mutex); m_error.clear(); }
        m_output = texture;
        m_fence = gl()->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        gl()->glFlush();
        if (!m_fence) { gl()->glFinish(); releaseOutput(true); }
        return copied;
    }
public:
    NativeTox(QString path, ToxSettings settings) : m_path(std::move(path)), m_settings(std::move(settings)) {}
    ~NativeTox() override {
        // Engine explicitly destroys the player on its render thread, with GL current.
        if (!QOpenGLContext::currentContext()) return; // never started (e.g. a project closed before its first render)
        GlState state;
        close();
        auto *g = gl();
        for (GLuint p : m_program) if (p) g->glDeleteProgram(p);
        if (m_vao) g->glDeleteVertexArrays(1, &m_vao);
        if (m_fbo) g->glDeleteFramebuffers(1, &m_fbo);
    }
    void configure(const ToxSettings &s) override { std::lock_guard<std::mutex> lock(m_mutex); m_settings = s; m_settings.sanitize(); }
    void reload() override { m_reload = true; }
    void pulse(const QString &id) override { std::lock_guard<std::mutex> lock(m_mutex); if (m_pulses.size() < 128) m_pulses << id; }
    std::vector<ToxControl> controls() const override { std::lock_guard<std::mutex> lock(m_mutex); return m_controls; }
    std::vector<std::pair<QString, QString>> inputs() const override { std::lock_guard<std::mutex> lock(m_mutex); return m_inputs; }
    std::vector<std::pair<QString, QString>> outputs() const override { std::lock_guard<std::mutex> lock(m_mutex); return m_outputs; }
    quint64 revision() const override { return m_revision; }
    QString error() const override { std::lock_guard<std::mutex> lock(m_mutex); return m_error; }
    QString status() const override { std::lock_guard<std::mutex> lock(m_mutex); return m_status; }
    void setInputTexture(const QString &id, GLuint t, int w, int h) override { m_images[id] = {t, w, h}; }
    bool update(Texture2D &dest) override {
        GlState state;
        if (m_reload.exchange(false)) {
            close();
            std::lock_guard<std::mutex> lock(m_mutex);
            m_error.clear(); m_status = QStringLiteral("Loading TouchEngine…");
        }
        releaseOutput(false);
        m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(), [](auto &slot) {
            if (!slot->released) return false;
            slot->image.destroy(); return true;
        }), m_retired.end());
        if (!m_started && !open()) return false;
        if (!m_loaded || !m_instance || !m_context) return false;
        if (!m_cooking && m_schema.exchange(false)) discover();
        ToxSettings s;
        { std::lock_guard<std::mutex> lock(m_mutex); s = m_settings; }
        bool copied = false;
        if (m_finished.load() && !m_output) {
            m_finished = false; m_cooking = false;
            copied = receive(dest, s);
        }
        if (!s.playing) { m_lastCook = -1; if (error().isEmpty()) message(QStringLiteral("TouchEngine paused")); return copied; }
        if (m_cooking || m_output) return copied;
        if (!m_resumed) {
            if (!TEOpenGLContextSupportsTexturesForInstance(m_context, m_instance)) {
                message(QStringLiteral("TouchEngine texture sharing is unavailable on this GPU"), true); return copied;
            }
            if (!check(TEInstanceResume(m_instance))) return copied;
            m_resumed = true;
        }
        const double now = m_clock.nsecsElapsed() / 1e9;
        if (m_lastCook >= 0 && now - m_lastCook < 1.0 / s.fps) return copied;
        // Follow elapsed time under load; pausing holds component time without a jump on resume.
        if (m_lastCook >= 0) m_time += now - m_lastCook;
        m_lastCook = now;
        for (const auto &c : controls()) {
            if (!s.values.contains(c.id) || c.type == ParamInfo::Type::Trigger) continue;
            const QByteArray id = c.id.toUtf8();
            if (c.type == ParamInfo::Type::Float) {
                std::vector<double> v; for (int k = 0; k < c.count; ++k) v.push_back(c.value(s, k).toDouble());
                check(TEInstanceLinkSetDoubleValue(m_instance, id.constData(), v.data(), c.count));
            } else if (c.type == ParamInfo::Type::Int) {
                std::vector<int32_t> v; for (int k = 0; k < c.count; ++k) v.push_back(c.value(s, k).toInt());
                check(TEInstanceLinkSetIntValue(m_instance, id.constData(), v.data(), c.count));
            } else if (c.type == ParamInfo::Type::Bool) check(TEInstanceLinkSetBooleanValue(m_instance, id.constData(), c.value(s).toBool()));
            else if (c.type == ParamInfo::Type::Text) check(TEInstanceLinkSetStringValue(m_instance, id.constData(), c.value(s).toString().toUtf8().constData()));
        }
        QStringList pulses;
        { std::lock_guard<std::mutex> lock(m_mutex); pulses.swap(m_pulses); }
        for (const QString &id : pulses) check(TEInstanceLinkSetBooleanValue(m_instance, id.toUtf8().constData(), true));
        sendInputs(s);
        check(TEInstanceSetFloatFrameRate(m_instance, float(s.fps)));
        m_cooking = true;
        if (!check(TEInstanceStartFrameAtTime(m_instance, int64_t(m_time * 1000000), 1000000, false))) m_cooking = false;
        // Pulse/momentary values are reset after scheduling; the SDK latches frame values.
        for (const QString &id : pulses) TEInstanceLinkSetBooleanValue(m_instance, id.toUtf8().constData(), false);
        if (error().isEmpty()) message(QStringLiteral("TouchEngine running"));
        return copied;
    }
};
} // namespace

bool touchEngineCompiledIn() { return true; }
std::unique_ptr<ToxPlayer> createNativeToxPlayer(const QString &path, const ToxSettings &s)
{
    return std::make_unique<NativeTox>(path, s);
}
#else
namespace {
class UnavailableTox final : public ToxPlayer {
public:
    bool update(Texture2D &) override { return false; }
    void configure(const ToxSettings &) override {}
    void reload() override {}
    void pulse(const QString &) override {}
    std::vector<ToxControl> controls() const override { return {}; }
    std::vector<std::pair<QString, QString>> inputs() const override { return {}; }
    std::vector<std::pair<QString, QString>> outputs() const override { return {}; }
    void setInputTexture(const QString &, GLuint, int, int) override {}
    quint64 revision() const override { return 0; }
    QString error() const override { return QStringLiteral("This build has no TouchEngine SDK. ") + touchEngineRequirements(); }
    QString status() const override { return error(); }
};
}
bool touchEngineCompiledIn() { return false; }
std::unique_ptr<ToxPlayer> createNativeToxPlayer(const QString &, const ToxSettings &) { return std::make_unique<UnavailableTox>(); }
#endif
