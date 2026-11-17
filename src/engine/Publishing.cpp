// The publishers of the viewports (Publishing.h)
#include "Publishing.h"

#include <algorithm>
#include <cmath>
#include <cstring>

struct Publishing::Publication {
    PublishSettings applied;
    bool init = false;
    std::unique_ptr<GpuPublisher> gpu[kPublishKindCount];
    std::shared_ptr<CpuPublisher> cpu[kPublishKindCount], tap;
    std::unique_ptr<CpuSendThread> sender;
    PublishState states[kPublishKindCount]; // guarded by m_stateMutex
    int announcedRate = 0;
    double rateSince = 0;
};

Publishing::Publishing() { m_clock.start(); }

Publishing::~Publishing() = default;

void Publishing::release()
{
    // The send threads are stopped before the CPU publishers go
    for (auto &[id, pub] : m_pubs) releasePublication(*pub);
    {
        std::lock_guard<std::mutex> lk(m_stateMutex);
        m_pubs.clear();
    }
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
}

void Publishing::share(quint64 viewport, const unsigned char *rgba, int width, int height)
{
    const auto it = m_pubs.find(viewport);
    if (it == m_pubs.end()) return;
    for (int ki : {int(PublishKind::Syphon), int(PublishKind::Spout)})
        if (it->second->gpu[ki]) it->second->gpu[ki]->publish(rgba, width, height);
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

void Publishing::updateReceivers(Publication &pub, quint64 frame)
{
    if ((frame % 30) != 0) return;
    std::lock_guard<std::mutex> lk(m_stateMutex);
    for (int ki = 0; ki < kPublishKindCount; ++ki) {
        if (pub.cpu[ki] && pub.cpu[ki]->liveState(&pub.states[ki])) continue; // it tells how it goes
        if (pub.states[ki].level != PublishState::Ok) continue;
        if (pub.cpu[ki]) pub.states[ki].receivers = pub.cpu[ki]->receivers();
        else if (pub.gpu[ki]) pub.states[ki].receivers = pub.gpu[ki]->receivers();
    }
}

int Publishing::announcedRate(Publication &pub, double fps)
{
    const int rate = standardRate(fps);
    const double nowS = m_clock.nsecsElapsed() / 1e9;
    if (pub.announcedRate == 0) pub.announcedRate = rate;
    if (rate == pub.announcedRate) pub.rateSince = nowS;
    else if (nowS - pub.rateSince > 2.0) pub.announcedRate = rate;
    return pub.announcedRate;
}

bool Publishing::wantsPixels(quint64 viewport) const
{
    const auto it = m_pubs.find(viewport);
    return it != m_pubs.end() && it->second->sender && it->second->sender->hasPublishers();
}

bool Publishing::wantsShare(quint64 viewport) const
{
    const auto it = m_pubs.find(viewport);
    if (it == m_pubs.end()) return false;
    for (int ki : {int(PublishKind::Syphon), int(PublishKind::Spout)})
        if (it->second->gpu[ki]) return true;
    return false;
}

void Publishing::submitPixels(quint64 viewport, const unsigned char *rgba, int width, int height, double fps, quint64 frame,
                              const std::optional<Timecode> &tc)
{
    const auto found = m_pubs.find(viewport);
    if (found == m_pubs.end()) return;
    Publication &pub = *found->second;
    updateReceivers(pub, frame);
    if (!pub.sender || !pub.sender->hasPublishers() || !rgba || width < 1 || height < 1) return;
    CpuFrame *out = pub.sender->acquire();
    if (!out) {
        ++m_readbacksSkipped;
        return;
    }
    out->width = width;
    out->height = height;
    out->stride = width * 4;
    out->bgra.resize(size_t(width) * height * 4);
    for (int y = 0; y < height; ++y) { // rows top to bottom, RGBA to BGRA, alpha opaque (the flip a server does)
        const unsigned char *src = rgba + size_t(height - 1 - y) * width * 4;
        unsigned char *dst = out->bgra.data() + size_t(y) * width * 4;
        for (int x = 0; x < width; ++x, src += 4, dst += 4) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = 255;
        }
    }
    out->timestamp100ns = qint64(m_clock.nsecsElapsed() / 100);
    out->fpsN = announcedRate(pub, fps);
    out->fpsD = 1;
    out->timecode = tc;
    pub.sender->submit(out);
}
