#pragma once
// The publishers of the viewports (NDI, OMT, Syphon, Spout; the tests' tap): one set per viewport, following its
// settings. Owned by the Engine; driven from the render thread, its states
// read from any thread. It knows nothing of the layers: the engine hands it each viewport's id, settings and picture.

#include "Publish.h"

#include <QElapsedTimer>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

class Publishing
{
public:
    Publishing();
    ~Publishing();

    // --- Render thread, context current
    // Every viewport's publishers follow its settings (in the list's order: the first one carries the test tap);
    // those of a viewport that is gone are stopped. Cheap when nothing changed
    void follow(const std::vector<std::pair<quint64, PublishSettings>> &viewports);
    // Syphon, Spout: the picture's pixels
    void share(quint64 viewport, const unsigned char *rgba, int width, int height);
    // Through QRhi: the pictures come as pixels (RGBA, the picture's bottom row first, as a texture holds them), read
    // back by the renderer. wantsPixels(): a NDI, OMT, DeckLink publisher or the tap is on (submitPixels); wantsShare():
    // a Syphon or Spout publisher is on (share())
    bool wantsPixels(quint64 viewport) const;
    bool wantsShare(quint64 viewport) const;
    void submitPixels(quint64 viewport, const unsigned char *rgba, int width, int height, double fps, quint64 frame,
                      const std::optional<Timecode> &tc = std::nullopt);
    quint64 readbacksSkipped() const { return m_readbacksSkipped.load(); } // NDI / OMT frames not sent: GPU late
    void release();       // every publisher stopped

    // --- Any thread
    PublishState state(quint64 viewport, PublishKind k) const;
    void setTap(std::function<void(const CpuFrame &)> fn); // tests: frames of the first viewport
    // Tests: Syphon / Spout publishers made by this (any platform, without the SDKs) instead of the real ones; set
    // before the publish settings that use them. nullptr: the real ones again
    using GpuPublisherFactory = std::function<std::unique_ptr<GpuPublisher>(PublishKind)>;
    void setGpuPublisherFactoryForTests(GpuPublisherFactory f);

private:
    struct Publication;
    void apply(Publication &pub, const PublishSettings &s);
    void releasePublication(Publication &pub);
    void setState(Publication &pub, PublishKind k, PublishState st);
    void updateReceivers(Publication &pub, quint64 frame);   // about twice per second
    int announcedRate(Publication &pub, double fps);          // changes only if the new value holds for 2 s

    std::map<quint64, std::unique_ptr<Publication>> m_pubs; // by viewport id (render thread; states: m_stateMutex)
    mutable std::mutex m_stateMutex;
    std::mutex m_tapMutex;
    std::function<void(const CpuFrame &)> m_tap;
    bool m_tapDirty = false;
    bool m_tapOn = false;      // a tap is set (render thread)
    quint64 m_tapViewport = 0; // the viewport carrying it (render thread; 0: none yet)
    std::mutex m_gpuFactoryMutex;
    GpuPublisherFactory m_gpuFactory; // tests (under m_gpuFactoryMutex)
    QElapsedTimer m_clock;
    std::atomic<quint64> m_readbacksSkipped{0};
};
