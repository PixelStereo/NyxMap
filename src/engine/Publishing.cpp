// The publishers of the viewports (Publishing.h)
#include "Publishing.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif

struct Publishing::Publication {
    PublishSettings applied;
    bool init = false;
    std::unique_ptr<GpuPublisher> gpu[kPublishKindCount];
    std::shared_ptr<CpuPublisher> cpu[kPublishKindCount], tap;
    std::unique_ptr<CpuSendThread> sender;
    PublishState states[kPublishKindCount]; // guarded by m_stateMutex
    RenderTarget readback;
    // The pixel buffers the pictures are read back into. Each one in turn: free; being written by the GPU (its fence
    // tells when it is done); then mapped and lent to the send thread, which copies it and gives it back
    static constexpr int kPbos = 3;
    enum class PboState { Free, Reading, Lent };
    GLuint pbo[kPbos] = {0, 0, 0};
    GLsync fence[kPbos] = {nullptr, nullptr, nullptr};
    PboState pboState[kPbos] = {PboState::Free, PboState::Free, PboState::Free};
    double pboTime[kPbos] = {0, 0, 0};
    std::optional<Timecode> pboTc[kPbos]; // the timecode of the picture in each buffer
    int pboW = 0, pboH = 0;
    int reading = -1; // the buffer the last picture was read into (-1: none)
    int announcedRate = 0;
    double rateSince = 0;
};

Publishing::Publishing(std::function<void()> drawQuad) : m_drawQuad(std::move(drawQuad)) { m_clock.start(); }

Publishing::~Publishing() = default;

bool Publishing::initGl(QString *log)
{
    // Vertical flip for readback (NDI / OMT expect top-to-bottom rows)
    m_flipProgram = compileProgram("#version 330 core\nlayout(location=0) in vec2 a_pos; out vec2 v_uv;\n"
                                   "void main(){ v_uv = vec2(a_pos.x*0.5+0.5, 0.5-a_pos.y*0.5); gl_Position = vec4(a_pos,0.0,1.0); }\n",
                                   "#version 330 core\nuniform sampler2D u_tex; in vec2 v_uv; out vec4 o;\n"
                                   "void main(){ o = vec4(texture(u_tex, v_uv).rgb, 1.0); }\n",
                                   log);
    if (!m_flipProgram) return false;
    m_flipTexLoc = gl()->glGetUniformLocation(m_flipProgram, "u_tex");
    return true;
}

void Publishing::release()
{
    // GPU publishers need the context, the send threads are stopped before the CPU publishers go
    for (auto &[id, pub] : m_pubs) releasePublication(*pub);
    {
        std::lock_guard<std::mutex> lk(m_stateMutex);
        m_pubs.clear();
    }
    if (m_flipProgram && QOpenGLContext::currentContext()) gl()->glDeleteProgram(m_flipProgram);
    m_flipProgram = 0;
    m_tapViewport = 0; // the tap, if on, goes back onto the first viewport at the next follow()
}

void Publishing::setTap(std::function<void(const CpuFrame &)> fn)
{
    std::lock_guard<std::mutex> lk(m_tapMutex);
    m_tap = std::move(fn);
    m_tapDirty = true;
}

PublishState Publishing::state(quint64 viewport, PublishKind k) const
{
    std::lock_guard<std::mutex> lk(m_stateMutex);
    auto it = m_pubs.find(viewport);
    return it == m_pubs.end() ? PublishState() : it->second->states[int(k)];
}

void Publishing::setState(Publication &pub, PublishKind k, PublishState st)
{
    std::lock_guard<std::mutex> lk(m_stateMutex);
    pub.states[int(k)] = std::move(st);
}

void Publishing::setGpuPublisherFactoryForTests(GpuPublisherFactory f)
{
    std::lock_guard<std::mutex> lk(m_gpuFactoryMutex);
    m_gpuFactory = std::move(f);
}

static bool isGpuKind(PublishKind k) { return k == PublishKind::Syphon || k == PublishKind::Spout; }

void Publishing::follow(const std::vector<std::pair<quint64, PublishSettings>> &viewports)
{
    // The test tap follows whichever viewport is first, not the one first when it was set: a viewport replaced
    // after that (a new project's) would take the tap away with its publication, and no frame would come again
    const quint64 first = viewports.empty() ? 0 : viewports.front().first;
    std::function<void(const CpuFrame &)> tap;
    bool tapDirty;
    {
        std::lock_guard<std::mutex> lk(m_tapMutex);
        tapDirty = std::exchange(m_tapDirty, false) || (m_tapOn && m_tapViewport != first);
        if (tapDirty) tap = m_tap;
    }
    if (tapDirty) {
        m_tapOn = bool(tap);
        m_tapViewport = first;
    }
    bool changed = tapDirty || viewports.size() != m_pubs.size();
    for (const auto &[id, s] : viewports) {
        if (changed) break;
        const auto it = m_pubs.find(id);
        changed = it == m_pubs.end() || !(it->second->applied == s);
    }
    if (!changed) return;

    for (auto it = m_pubs.begin(); it != m_pubs.end();) {
        const bool alive = std::any_of(viewports.begin(), viewports.end(), [&](const auto &w) { return w.first == it->first; });
        if (alive) {
            ++it;
            continue;
        }
        releasePublication(*it->second);
        std::lock_guard<std::mutex> lk(m_stateMutex);
        it = m_pubs.erase(it);
    }
    for (size_t k = 0; k < viewports.size(); ++k) {
        Publication *current;
        {
            // Readers must never see a newly inserted null slot, and map insertion
            // must use the same mutex as state()'s lookup.
            std::lock_guard<std::mutex> lk(m_stateMutex);
            auto &slot = m_pubs[viewports[k].first];
            if (!slot) slot = std::make_unique<Publication>();
            current = slot.get();
        }
        Publication &pub = *current;
        // The test tap follows the first viewport
        if (k == 0 && tapDirty) {
            pub.tap = tap ? std::shared_ptr<CpuPublisher>(createTapPublisher(tap)) : nullptr;
            pub.init = false; // the send thread is rebuilt below
        } else if (k != 0 && pub.tap) {
            pub.tap.reset();
            pub.init = false;
        }
        apply(pub, viewports[k].second);
    }
}

void Publishing::apply(Publication &pub, const PublishSettings &s)
{
    const bool first = !pub.init;
    pub.init = true;
    bool cpuChanged = first;
    for (int ki = 0; ki < kPublishKindCount; ++ki) {
        const PublishKind k = PublishKind(ki);
        const PublishTarget &want = s.targets[ki];
        const PublishTarget &had = pub.applied.targets[ki];
        const bool optionsChanged = s.libraryFolder != pub.applied.libraryFolder
                                    || (k == PublishKind::Omt && s.omtQuality != pub.applied.omtQuality)
                                    || (k == PublishKind::DeckLink && s.decklinkMode != pub.applied.decklinkMode);
        if (!first && want == had && !(want.enabled && optionsChanged)) continue;

        if (isGpuKind(k)) {
            pub.gpu[ki].reset();
        } else if (pub.cpu[ki]) {
            pub.cpu[ki].reset();
            cpuChanged = true;
        }
        if (!want.enabled) {
            setState(pub, k, {PublishState::Off, QStringLiteral("Disabled"), -1});
            continue;
        }
        GpuPublisherFactory testFactory;
        if (isGpuKind(k)) {
            std::lock_guard<std::mutex> lk(m_gpuFactoryMutex);
            testFactory = m_gpuFactory;
        }
        if (!publishCompiledIn(k) && !testFactory) {
            setState(pub, k,
                     {PublishState::Unavailable,
                      k == PublishKind::Syphon   ? QStringLiteral("Syphon is only available on macOS")
                      : k == PublishKind::Spout  ? QStringLiteral("Spout is only available on Windows")
                                                 : QStringLiteral("Built without DeckLink support"),
                      -1});
            continue;
        }
        QString err;
        if (isGpuKind(k)) {
            auto p = testFactory ? testFactory(k) : k == PublishKind::Syphon ? createSyphonPublisher() : createSpoutPublisher();
            if (p && p->start(want.name, &err)) {
                pub.gpu[ki] = std::move(p);
                setState(pub, k, {PublishState::Ok, QStringLiteral("Active: \"%1\"").arg(want.name), -1});
            } else {
                setState(pub, k, {PublishState::Error, err.isEmpty() ? QStringLiteral("Failed to start") : err, -1});
            }
        } else {
            std::shared_ptr<CpuPublisher> p(k == PublishKind::Ndi   ? createNdiPublisher()
                                            : k == PublishKind::Omt ? createOmtPublisher()
                                                                    : createDeckLinkPublisher());
            if (p && p->start(want.name, s, &err)) {
                pub.cpu[ki] = p;
                cpuChanged = true;
                PublishState st{PublishState::Ok, QStringLiteral("Active: \"%1\"").arg(want.name), 0};
                p->liveState(&st);
                setState(pub, k, st);
            } else {
                setState(pub, k, {PublishState::Error, err, -1});
            }
        }
    }
    pub.applied = s;
    if (cpuChanged) {
        std::vector<std::shared_ptr<CpuPublisher>> pubs;
        for (auto &p : pub.cpu)
            if (p) pubs.push_back(p);
        if (pub.tap) pubs.push_back(pub.tap);
        if (!pubs.empty() && !pub.sender) pub.sender = std::make_unique<CpuSendThread>();
        if (pub.sender) pub.sender->setPublishers(std::move(pubs));
    }
}

void Publishing::releasePublication(Publication &pub)
{
    for (auto &p : pub.gpu) p.reset();
    if (pub.sender) pub.sender->setPublishers({});
    pub.sender.reset();
    for (auto &p : pub.cpu) p.reset();
    pub.tap.reset();
    pub.readback.destroy();
    for (GLsync &s : pub.fence)
        if (s) {
            gl()->glDeleteSync(s);
            s = nullptr;
        }
    // The send thread is gone: nothing reads the mapped buffers any more (deleting one unmaps it)
    if (pub.pbo[0]) gl()->glDeleteBuffers(Publication::kPbos, pub.pbo);
    for (int k = 0; k < Publication::kPbos; ++k) {
        pub.pbo[k] = 0;
        pub.pboState[k] = Publication::PboState::Free;
    }
    pub.reading = -1;
}

bool Publishing::sharing() const
{
    for (const auto &[id, pub] : m_pubs)
        for (int ki : {int(PublishKind::Syphon), int(PublishKind::Spout)})
            if (pub->gpu[ki]) return true;
    return false;
}

void Publishing::share(quint64 viewport, const RenderTarget &picture)
{
    const auto it = m_pubs.find(viewport);
    if (it == m_pubs.end()) return;
    for (int ki : {int(PublishKind::Syphon), int(PublishKind::Spout)})
        if (it->second->gpu[ki]) it->second->gpu[ki]->publish(picture.tex, picture.w, picture.h);
}

static int standardRate(double fps)
{
    static const int rates[] = {24, 25, 30, 48, 50, 60, 72, 75, 90, 100, 120};
    if (fps < 1.0) return 60;
    int best = 60;
    double bestD = 1e9;
    for (int r : rates) {
        const double d = std::fabs(fps - r);
        if (d < bestD) {
            bestD = d;
            best = r;
        }
    }
    return best;
}

void Publishing::readback(quint64 viewport, const RenderTarget &out, double fps, quint64 frame,
                          const std::optional<Timecode> &tc)
{
    const auto found = m_pubs.find(viewport);
    if (found == m_pubs.end()) return;
    Publication &pub = *found->second;
    // Receiver count, about twice per second
    if ((frame % 30) == 0) {
        std::lock_guard<std::mutex> lk(m_stateMutex);
        for (int ki = 0; ki < kPublishKindCount; ++ki) {
            if (pub.cpu[ki] && pub.cpu[ki]->liveState(&pub.states[ki])) continue; // it tells how it goes
            if (pub.states[ki].level != PublishState::Ok) continue;
            if (pub.cpu[ki]) pub.states[ki].receivers = pub.cpu[ki]->receivers();
            else if (pub.gpu[ki]) pub.states[ki].receivers = pub.gpu[ki]->receivers();
        }
    }

    using PboState = Publication::PboState;
    auto f = gl();
    // The buffers the send thread is done with: unmapped, free again
    auto takeBack = [&] {
        if (!pub.sender) return;
        const unsigned returned = pub.sender->takeReturned();
        for (int k = 0; k < Publication::kPbos; ++k)
            if ((returned & (1u << k)) && pub.pboState[k] == PboState::Lent) {
                f->glBindBuffer(GL_PIXEL_PACK_BUFFER, pub.pbo[k]);
                f->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                pub.pboState[k] = PboState::Free;
            }
        f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    };
    takeBack();
    // A read not collected (no publisher any more, the GPU late): its buffer is free again
    auto dropReading = [&] {
        if (pub.reading < 0) return;
        if (pub.fence[pub.reading]) f->glDeleteSync(pub.fence[pub.reading]);
        pub.fence[pub.reading] = nullptr;
        if (pub.pboState[pub.reading] == PboState::Reading) pub.pboState[pub.reading] = PboState::Free;
        pub.reading = -1;
    };
    if (!pub.sender || !pub.sender->hasPublishers() || !out.tex) {
        dropReading();
        return;
    }
    const int w = out.w, h = out.h;
    const GLsizeiptr bytes = GLsizeiptr(w) * h * 4;
    if (!pub.pbo[0] || w != pub.pboW || h != pub.pboH) {
        // Another size: the send thread lets go of the buffers it reads before they are made again
        pub.sender->recall();
        takeBack();
        dropReading();
        if (!pub.pbo[0]) f->glGenBuffers(Publication::kPbos, pub.pbo);
        for (GLuint b : pub.pbo) {
            f->glBindBuffer(GL_PIXEL_PACK_BUFFER, b);
            f->glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
        }
        f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        pub.pboW = w;
        pub.pboH = h;
    }
    pub.readback.ensure(w, h);
    // On allocation failure ensure() deliberately preserves its old
    // framebuffer. That older texture may be a different size or precision:
    // never draw/read w*h pixels into/from it (or framebuffer zero).
    if (!pub.readback.ready(w, h)) {
        ++m_readbacksSkipped;
        return; // keep the last successfully sent picture; retry next frame
    }

    // 1) Current frame flipped (top-to-bottom rows)
    pub.readback.bind();
    f->glDisable(GL_BLEND);
    f->glUseProgram(m_flipProgram);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, out.tex);
    f->glUniform1i(m_flipTexLoc, 0);
    m_drawQuad();

    // 2) Previous frame, normally available by now: its buffer mapped and lent to the send thread, which copies it
    // (the render thread no longer copies the picture). Its read back not done yet (the GPU late): waited for 2 ms at
    // most, then that frame is not sent (counted) rather than the render thread held up by the mapping
    if (pub.reading >= 0) {
        const int k = pub.reading;
        bool ready = true;
        if (pub.fence[k]) {
            const GLenum r = f->glClientWaitSync(pub.fence[k], GL_SYNC_FLUSH_COMMANDS_BIT, 2000000);
            ready = r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED;
        }
        if (ready) {
            if (pub.fence[k]) f->glDeleteSync(pub.fence[k]);
            pub.fence[k] = nullptr;
            pub.reading = -1;
            pub.pboState[k] = PboState::Free;
            f->glBindBuffer(GL_PIXEL_PACK_BUFFER, pub.pbo[k]);
            if (const void *ptr = f->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT)) {
                LentFrame lent;
                lent.data = static_cast<const uint8_t *>(ptr);
                lent.bytes = size_t(bytes);
                lent.token = k;
                lent.width = w;
                lent.height = h;
                lent.stride = w * 4;
                lent.timestamp100ns = qint64(pub.pboTime[k]);
                lent.timecode = pub.pboTc[k];
                // Announced frame rate: only changes if the new value holds for 2 s
                const int rate = standardRate(fps);
                const double nowS = m_clock.nsecsElapsed() / 1e9;
                if (pub.announcedRate == 0) pub.announcedRate = rate;
                if (rate == pub.announcedRate) pub.rateSince = nowS;
                else if (nowS - pub.rateSince > 2.0) pub.announcedRate = rate;
                lent.fpsN = pub.announcedRate;
                lent.fpsD = 1;
                pub.pboState[k] = PboState::Lent;
                pub.sender->lend(lent);
                takeBack(); // the one it replaced, if the send thread is late
            }
        } else {
            ++m_readbacksSkipped;
            dropReading();
        }
    }

    // 3) Current frame read back asynchronously into a free buffer. None free (the send thread still copies one, the
    // next waits): this frame is not sent
    int target = -1;
    for (int k = 0; k < Publication::kPbos && target < 0; ++k)
        if (pub.pboState[k] == PboState::Free) target = k;
    if (target >= 0) {
        pub.readback.bind();
        f->glBindBuffer(GL_PIXEL_PACK_BUFFER, pub.pbo[target]);
        f->glPixelStorei(GL_PACK_ALIGNMENT, 4);
        f->glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        if (pub.fence[target]) f->glDeleteSync(pub.fence[target]);
        pub.fence[target] = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        pub.pboState[target] = PboState::Reading;
        pub.pboTime[target] = double(m_clock.nsecsElapsed() / 100);
        pub.pboTc[target] = tc;
        pub.reading = target;
    } else {
        ++m_readbacksSkipped;
    }
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
