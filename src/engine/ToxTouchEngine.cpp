// TouchEngine SDK integration. SDK copyright and terms: licenses/touchengine.txt.
#include "Tox.h"
#include <rhi/qrhi.h>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>
#include <atomic>
#include <array>
#include <cmath>
#include <mutex>

#ifdef NYXMAP_HAS_TOUCHENGINE
#include <TouchEngine/TEObject.h>
// Through QRhi's own device: Metal on macOS (ToxMetal), Direct3D 11 on Windows (ToxD3D11)
#if defined(Q_OS_MACOS)
#include "ToxMetal.h"
using ToxRhiBackend = ToxMetal;
#define NYXMAP_TOX_RHI
#elif defined(Q_OS_WIN)
#include "ToxD3D11.h"
using ToxRhiBackend = ToxD3D11;
#define NYXMAP_TOX_RHI
#endif

namespace {
template<class T> struct Ref {
    T *p = nullptr;
    ~Ref() { TERelease(&p); }
    Ref() = default;
    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
};

class NativeTox final : public ToxPlayer {
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
    QElapsedTimer m_clock;
    double m_lastCook = -1, m_time = 0;
#ifdef NYXMAP_TOX_RHI
    // Through QRhi's device: chosen by the first takeTexture()
    std::unique_ptr<ToxRhiBackend> m_metal; // the backend's own context and textures
    QRhi *m_rhi = nullptr;
    QMap<QString, QRhiTexture *> m_rhiImages;
    QRhiTexture *m_picture = nullptr;
    QSize m_pictureSize;
    bool m_pictureFlipped = false;
#endif
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
    void close() {
        if (m_metal) m_metal->releaseOutput(m_instance);
        m_picture = nullptr;
        if (m_instance && m_cooking) TEInstanceCancelFrame(m_instance);
        TERelease(&m_instance); // joins callbacks before their owner is destroyed
        m_metal.reset(); // its textures went with the instance's last use
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
        if (!m_rhi) { message(QStringLiteral("TouchEngine needs the QRhi renderer"), true); return false; }
        QString why; // Metal or Direct3D 11, on QRhi's own device
        m_metal = ToxRhiBackend::create(m_rhi, &why);
        if (!m_metal) { message(why.isEmpty() ? QStringLiteral("TouchEngine cannot use QRhi's device") : why, true); return false; }
        if (!check(TEInstanceAssociateGraphicsContext(m_instance, m_metal->context()))) return false;
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
    void sendInputs(const ToxSettings &s) {
        for (const auto &top : inputs()) {
            const QByteArray id = top.first.toUtf8();
            QRhiTexture *source = s.inputs.value(top.first) ? m_rhiImages.value(top.first) : nullptr;
            QString why;
            if (!m_metal->setInput(m_instance, id.constData(), source, &why)) message(why, true);
        }
    }
#ifdef NYXMAP_TOX_RHI
    // The output TOP's newest picture as a QRhi texture
    bool receiveRhi(const ToxSettings &s) {
        const auto tops = outputs();
        QString selected = s.output;
        if (selected.isEmpty() && !tops.empty()) selected = tops.front().first;
        if (selected.isEmpty()) { message(QStringLiteral("The component exposes no output TOP"), true); return false; }
        if (std::none_of(tops.begin(), tops.end(), [&](const auto &p) { return p.first == selected; })) {
            message(QStringLiteral("Output TOP is no longer available: ") + selected, true); return false;
        }
        QString why;
        bool flipped = false;
        QSize size;
        QRhiTexture *picture = m_metal->receive(m_instance, selected.toUtf8().constData(), &size, &flipped, &why);
        if (!picture) {
            if (!why.isEmpty()) message(why, true);
            return false;
        }
        m_picture = picture;
        m_pictureSize = size;
        m_pictureFlipped = flipped;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error.clear();
        return true;
    }
#endif
public:
    NativeTox(QString path, ToxSettings settings) : m_path(std::move(path)), m_settings(std::move(settings)) {}
    ~NativeTox() override { close(); }
    void configure(const ToxSettings &s) override { std::lock_guard<std::mutex> lock(m_mutex); m_settings = s; m_settings.sanitize(); }
    void advance(double dt) override { if (std::isfinite(dt) && dt > 0) m_time += dt; }
    void reload() override { m_reload = true; }
    void pulse(const QString &id) override { std::lock_guard<std::mutex> lock(m_mutex); if (m_pulses.size() < 128) m_pulses << id; }
    std::vector<ToxControl> controls() const override { std::lock_guard<std::mutex> lock(m_mutex); return m_controls; }
    std::vector<std::pair<QString, QString>> inputs() const override { std::lock_guard<std::mutex> lock(m_mutex); return m_inputs; }
    std::vector<std::pair<QString, QString>> outputs() const override { std::lock_guard<std::mutex> lock(m_mutex); return m_outputs; }
    quint64 revision() const override { return m_revision; }
    QString error() const override { std::lock_guard<std::mutex> lock(m_mutex); return m_error; }
    QString status() const override { std::lock_guard<std::mutex> lock(m_mutex); return m_status; }
    // Through QRhi's Metal device: the newest picture (the last one while there is no new)
    QRhiTexture *takeTexture(QRhi *rhi, QSize *size, bool *flipped) override {
#ifdef NYXMAP_TOX_RHI
        if (!usesRhi(rhi)) return nullptr;
        m_rhi = rhi;
        run();
        *size = m_pictureSize;
        *flipped = m_pictureFlipped;
        return m_picture;
#else
        (void)rhi; (void)size; (void)flipped;
        return nullptr;
#endif
    }
    bool usesRhi(QRhi *rhi) const override {
#ifdef NYXMAP_TOX_RHI
        return rhi && rhi->backend() == ToxRhiBackend::kBackend;
#else
        (void)rhi;
        return false;
#endif
    }
    void setInputRhiTexture(const QString &id, QRhiTexture *t) override {
#ifdef NYXMAP_TOX_RHI
        m_rhiImages[id] = t;
#else
        (void)id; (void)t;
#endif
    }
    bool run() {
        if (m_reload.exchange(false)) {
            close();
            std::lock_guard<std::mutex> lock(m_mutex);
            m_error.clear(); m_status = QStringLiteral("Loading TouchEngine…");
        }
        if (!m_started && !open()) return false;
        if (!m_loaded || !m_instance || !m_metal) return false;
        if (!m_cooking && m_schema.exchange(false)) discover();
        ToxSettings s;
        { std::lock_guard<std::mutex> lock(m_mutex); s = m_settings; }
        bool copied = false;
        if (m_finished.load()) {
            m_finished = false; m_cooking = false;
            copied = receiveRhi(s);
        }
        if (!s.playing) { m_lastCook = -1; if (error().isEmpty()) message(QStringLiteral("TouchEngine paused")); return copied; }
        if (m_cooking) return copied;
        if (!m_resumed) {
            if (!check(TEInstanceResume(m_instance))) return copied;
            m_resumed = true;
        }
        const double now = m_clock.nsecsElapsed() / 1e9;
        if (m_lastCook >= 0 && now - m_lastCook < 1.0 / s.fps) return copied;
        // Wall time paces requests; component time follows the engine's show clock.
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
    void configure(const ToxSettings &) override {}
    void reload() override {}
    void pulse(const QString &) override {}
    std::vector<ToxControl> controls() const override { return {}; }
    std::vector<std::pair<QString, QString>> inputs() const override { return {}; }
    std::vector<std::pair<QString, QString>> outputs() const override { return {}; }
    quint64 revision() const override { return 0; }
    QString error() const override { return QStringLiteral("This build has no TouchEngine SDK. ") + touchEngineRequirements(); }
    QString status() const override { return error(); }
};
}
bool touchEngineCompiledIn() { return false; }
std::unique_ptr<ToxPlayer> createNativeToxPlayer(const QString &, const ToxSettings &) { return std::make_unique<UnavailableTox>(); }
#endif
