// Engine: life of the engine itself — OpenGL context and render thread, the runGl() task protocol,
// the output window, the composition size, and the composition opacity / blackout fades.
// The rest of its methods live in Layers.cpp, Render.cpp, Project.cpp, Media.cpp and Snapshots.cpp.
#ifdef __APPLE__
#include <pthread.h>
#include <pthread/qos.h>
#endif
#include "EngineInternal.h"
#include "RhiDevice.h"
#include "RhiRenderer.h"
#include "Midi.h"
#include "ProcessMemory.h"
#include "VideoBudget.h"
#include "VideoDecoder.h"
#include "WorkerPool.h"

#include <QDebug>
#include <QJsonArray>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QThread>
#include <QWindow>
#include <cmath>

#ifdef Q_OS_MACOS
#include <OpenGL/OpenGL.h> // GL_SILENCE_DEPRECATION: set by CMake
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

// ---------------------------------------------------------------------------
// Render thread, OpenGL context, start and stop
// ---------------------------------------------------------------------------
std::atomic<bool> Engine::s_failRenderStart{false};

class RenderThread : public QThread
{
public:
    explicit RenderThread(Engine *e) : m_engine(e) { setObjectName("NyxMap-render"); }

protected:
    void run() override { m_engine->renderLoop(); }

private:
    Engine *m_engine;
};

Engine::Engine(QObject *parent)
    : QObject(parent), m_audio(std::make_unique<AudioOutput>()), m_sequencer(new Sequencer(*this, m_mutex)),
      m_tcRouter(std::make_unique<TimecodeRouter>())
{
    connect(this, &Engine::timecodeRoutingChanged, this, &Engine::updateTimecodeRouting);
    connect(this, &Engine::layersChanged, this, &Engine::updateTimecodeRouting); // a layer gone, or back (undo)
}

bool Engine::startAudio(const QString &device, QString *err, bool nullDevice)
{
    return m_audio->start(device, err, nullDevice);
}

void Engine::stopAudio() { m_audio->stop(); }

Engine::~Engine()
{
    m_sequencer->shutdownClock(); // the cue clock recalls snapshots on its own thread: stopped before anything goes
    m_midiIns.clear(); // their callbacks feed the interactions: stopped first
    shutdown();
}

bool Engine::initialize(QString *err)
{
    Q_UNUSED(err)
    m_ownerThread = QThread::currentThread();
    // The graphics API (QRhi) is chosen by the render thread, at its first frame (Engine::frame)
    m_library.scan();
    m_clock.start();
    ensureViewport(); // the composition is always shown through at least one viewport
    ensureSequence(); // and GO has a sequence from the start, as a new project
    m_lastNs = m_clock.nsecsElapsed();
    m_initialized = true;
    return true;
}

bool Engine::start()
{
    if (!m_initialized || m_threaded) return m_threaded;
    m_quit = false;
    {
        std::lock_guard<std::mutex> lk(m_taskMutex);
        m_renderFailed = false;
        m_tasksClosed = false;
    }
    m_thread = new RenderThread(this);
    m_threaded = true;
    m_thread->start(QThread::HighPriority);
    return true;
}

void Engine::stop()
{
    if (!m_threaded) return;
    m_quit = true;
    m_taskCv.notify_all();
    m_thread->wait();
    tsanAcquire(&m_thread);
    delete m_thread;
    m_thread = nullptr;
    m_threaded = false;
}

void Engine::shutdown()
{
    // The cue clock recalls snapshots on its own thread, and a recall may wait for the render thread: stopped first
    m_sequencer->shutdownClock();
    m_audio->stop();
    m_audioInput->stop();
    if (!m_initialized) return;
    stop();
    if (!m_released) releaseAll(); // not yet released by the render thread
    m_initialized = false;
}

// ---------------------------------------------------------------------------
// Render thread tasks (runGl)
// ---------------------------------------------------------------------------
void Engine::runGl(std::function<void()> fn, bool wait)
{
    if (!m_threaded) {
        fn();
        return;
    }
    if (QThread::currentThread() == m_thread) {
        fn();
        return;
    }
    auto task = std::make_shared<Task>();
    task->fn = std::move(fn);
    std::unique_lock<std::mutex> lk(m_taskMutex);
    if (m_renderFailed) return; // the render thread could not start: nothing runs there
    // It has quit: nobody would run this one or wake its caller. A release waits for the next start (if any)
    if (m_tasksClosed && wait) return;
    m_tasks.push_back(task);
    if (!wait) return;
    m_taskCv.wait(lk, [&] { return task->done || !m_threaded; });
}

void Engine::runPendingTasks()
{
    std::deque<std::shared_ptr<Task>> tasks;
    {
        std::lock_guard<std::mutex> lk(m_taskMutex);
        tasks.swap(m_tasks);
    }
    if (tasks.empty()) return;
    for (auto &t : tasks) {
        t->fn();
        t->fn = nullptr;
    }
    {
        std::lock_guard<std::mutex> lk(m_taskMutex);
        for (auto &t : tasks) t->done = true;
    }
    m_taskCv.notify_all();
}

void Engine::renderLoop()
{
    if (s_failRenderStart.exchange(false)) {
        qCritical("Render thread: unable to start.");
        // Nothing will be rendered: the tasks waiting, and those asked from now on, return without running
        {
            std::lock_guard<std::mutex> lk(m_taskMutex);
            m_renderFailed = true;
            for (auto &t : m_tasks) t->done = true;
            m_tasks.clear();
        }
        m_taskCv.notify_all();
        tsanRelease(&m_thread);
        return;
    }
#ifdef __APPLE__
    // The frame's deadlines are short: the highest quality of service gives its sleeps the tightest timer leeway
    // (a loaded or virtual machine otherwise wakes a 14 ms sleep after several periods)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    QElapsedTimer pace;
    while (!m_quit) {
        pace.start();
        runPendingTasks();

        // Pace: the frame rate chosen; else the screen of the output window shown (the last one, which waits for its
        // vertical sync); else the main screen's refresh rate. A frame its output does not show (no surface from
        // macOS this time) is not followed by one at another screen's pace.
        const double rate = effectiveRender().frameRate;
        const bool uncapped = rate == RenderSettings::UncappedFrameRate;
        const double hz = rate > 0 ? rate : m_presentHz > 1 ? m_presentHz : screenRefreshRate();
        const qint64 periodNs = uncapped ? 0 : qint64(1e9 / std::clamp(hz, 1.0, 1000.0));

        const qint64 t0 = FrameTiming::nowNs();
        frame(nextDt());
        const qint64 t1 = FrameTiming::nowNs();
        const bool presented = presentViewports(!uncapped);
        const qint64 t2 = FrameTiming::nowNs();
        m_frameTiming.noteFrame(t0, t1, t2, periodNs, presented);
        sampleResources();
        if (!m_framePending.exchange(true)) emit frameRendered();

        // The vertical sync held this frame (it waited for its screen): it is the pace, nothing to add. Otherwise the
        // next frame is due one period after this one was (absolute deadlines: a late wake-up is caught up on the next
        // frame instead of adding up; more than a period late, the count starts again from now)
        if (uncapped) {
            m_paceNext = -1;
            continue;
        }
        const bool heldBySync = rate == 0 && presented && t2 - t1 >= 4000000;
        const qint64 sleepNs = heldBySync ? FramePacer::resync(m_paceNext, t2, periodNs)
                                          : FramePacer::next(m_paceNext, t0, FrameTiming::nowNs(), periodNs);
        if (sleepNs > 0) QThread::usleep(static_cast<unsigned long>(sleepNs / 1000));
        (void)pace;
    }
    runPendingTasks();
    releaseAll();
    {
        // Release any callers still waiting; those to come return at once (the render thread ends here)
        std::lock_guard<std::mutex> lk(m_taskMutex);
        m_tasksClosed = true;
        for (auto &t : m_tasks) t->done = true;
        m_tasks.clear();
    }
    m_taskCv.notify_all();
    tsanRelease(&m_thread); // what the render thread did is done before stop() goes on
}

void Engine::releaseAll()
{
    // QRhi and every resource it owns are thread-affine. The renderer is created and used by the
    // render thread in threaded mode, so destroy it here, before that thread exits (not later in
    // Engine's member destruction on the UI/owner thread).
    m_rhi.reset();
    // Output windows (the windows themselves belong to the interface)
    m_outWindows.clear();
    // Source transitions in progress, and those over (render thread: released here at once, their decoders closed)
    for (auto &[id, t] : m_transitions) retireTransition(std::move(t));
    m_transitions.clear();
    // Publishers: their send threads
    m_publishing.release();
    // Pixel mapping: the sACN streams terminated
    m_dmx.stop();
    m_sampler.release();
    m_fixturesFollowed = 0;
    {
        Lock lk(&m_mutex);
        releaseRetired(true);
        for (auto &l : m_layers) releaseLayer(*l);
        m_layers.clear();
    }
    m_released = true;
}

// ---------------------------------------------------------------------------
// Output window
// ---------------------------------------------------------------------------
void Engine::setViewportWindow(quint64 viewport, QWindow *w)
{
    runGl([this, viewport, w] {
        auto it = m_outWindows.find(viewport);
        if (it != m_outWindows.end() && it->second.window != w) {
            releaseOutputSurface(it->second); // before the window goes
            m_outWindows.erase(it);
        }
        if (w && !m_outWindows.count(viewport)) {
            OutputSurface o;
            o.window = w;
            m_outWindows[viewport] = std::move(o);
        }
    });
}

void Engine::setViewportExposed(quint64 viewport, bool exposed, QSize pixelSize, double refreshHz)
{
    runGl([this, viewport, exposed, pixelSize, refreshHz] {
        auto it = m_outWindows.find(viewport);
        if (it == m_outWindows.end()) return;
        it->second.exposed = exposed;
        it->second.pixels = pixelSize;
        if (refreshHz > 1) it->second.hz = refreshHz;
    });
}

// Its swapchain goes before the window does
void Engine::releaseOutputSurface(OutputSurface &o)
{
    if (m_rhi && o.window) m_rhi->releaseOutputs(o.window);
}

QSurface::SurfaceType Engine::outputSurfaceType() const { return rhidevice::surfaceType(); }

void Engine::setCapturePresentedForTests(bool on)
{
    runGl([this, on] {
        m_capturePresented = on;
        if (m_rhi) m_rhi->setCapturePresented(on);
    });
}

QImage Engine::presentedForTests(quint64 viewport)
{
    QImage img;
    runGl([this, viewport, &img] {
        if (m_rhi) img = m_rhi->lastPresented(viewport);
    });
    return img;
}

bool Engine::presentViewports(bool syncToDisplay)
{
    std::vector<std::pair<const Layer *, OutputSurface *>> shown;
    {
        Lock lk(&m_mutex);
        for (const auto &l : m_layers) {
            if (!l->isViewport) continue;
            auto it = m_outWindows.find(l->id);
            if (it != m_outWindows.end() && it->second.window && it->second.exposed) shown.push_back({l.get(), &it->second});
        }
    }
    bool any = false;
    // The pace of the window that waits for its screen (the last one): the render follows it while it is shown
    m_presentHz = shown.empty() ? 0 : shown.back().second->hz;
    if (!m_rhi) return false; // a swapchain per window (none before the first frame)
    for (size_t k = 0; k < shown.size(); ++k) {
        OutputSurface &out = *shown[k].second;
        if (m_rhi->present(shown[k].first->id, out.window, syncToDisplay && k + 1 == shown.size())) {
            any = true;
            ++out.presented;
        } else {
            ++out.noPicture;
        }
    }
    return any;
}

quint64 Engine::windowFrames(quint64 viewport)
{
    quint64 n = 0;
    runGl([this, viewport, &n] {
        const auto it = m_outWindows.find(viewport);
        if (it != m_outWindows.end()) n = it->second.presented;
    });
    return n;
}

void Engine::setPreviewSource(int slot, quint64 id)
{
    Lock lk(&m_mutex);
    if (slot >= 0 && slot < int(m_previewSlots.size())) {
        m_previewSlots[size_t(slot)].id = id;
        m_previewSlots[size_t(slot)].asked = true;
    }
}

void Engine::setPreviewScene(int slot, bool on, const SceneSettings &camera, QSize size, Scene3D::ViewLook look)
{
    Lock lk(&m_mutex);
    if (slot < 0 || slot >= int(m_previewSlots.size())) return;
    auto &p = m_previewSlots[size_t(slot)];
    p.sceneView = on;
    p.camera = camera;
    p.sceneSize = size.boundedTo(QSize(2048, 2048)).expandedTo(QSize(1, 1));
    p.sceneLook = look;
}

QImage Engine::previewImage(int slot) const
{
    if (slot < 0 || slot >= int(m_previewImages.size())) return QImage();
    std::lock_guard<std::mutex> lk(m_previewMutex);
    return m_previewImages[size_t(slot)];
}

QImage Engine::compositionImage() const
{
    std::lock_guard<std::mutex> lk(m_previewMutex);
    return m_compositionImage;
}

void Engine::wantCompositionImage() { m_compositionWantedUntilNs = qint64(RealtimeTiming::nowNs()) + 500'000'000; }

// ---------------------------------------------------------------------------
// Composition size, opacity and blackout, source preview
// ---------------------------------------------------------------------------
QSize Engine::compositionSize() const
{
    Lock lk(&m_mutex);
    return m_compSize;
}

void Engine::setCompositionSize(QSize s)
{
    s = s.expandedTo(QSize(16, 16)).boundedTo(QSize(16384, 16384));
    {
        Lock lk(&m_mutex);
        if (s == m_compSize) return;
        m_compSize = s;
        for (auto &l : m_layers) l->mapping.aspect = double(s.width()) / s.height(); // their rotations are in its pixels
    }
    emit compositionSizeChanged(s);
}

void Engine::updateTimeScale() { timeScale() = paused() ? 0.0 : m_compositionSpeed.load(); }

void Engine::setCompositionSpeed(double speed)
{
    Lock lk(&m_mutex);
    m_compositionSpeed = std::clamp(speed, 0.0, 10.0);
    updateTimeScale();
}

void Engine::setPaused(bool on)
{
    Lock lk(&m_mutex);
    m_emergencyHeld = false;
    m_paused = on;
    updateTimeScale();
}

void Engine::resumeAfterStop()
{
    Lock lk(&m_mutex);
    if (m_emergencyHeld.exchange(false)) updateTimeScale();
}

double Engine::compositionOpacityTarget() const
{
    Lock lk(&m_mutex);
    return m_compositionOpacityTarget;
}

void Engine::setAudioVolume(float v)
{
    {
        Lock lk(&m_mutex);
        m_compFade.volume = false;
    }
    m_audio->setVolume(v);
}

void Engine::fadeCompositionOpacity(double target, double seconds)
{
    Lock lk(&m_mutex);
    m_compFade.opacity = false; // the fader takes over from a snapshot's fade
    m_compositionOpacityTarget = std::clamp(target, 0.0, 1.0);
    m_compositionOpacitySpeed = seconds > 0.0 ? 1.0 / seconds : 0.0;
    m_compositionFaderClock.reset(RealtimeTiming::nowNs());
}

void Engine::setBlackout(bool on, double seconds)
{
    {
        Lock lk(&m_mutex);
        m_blackTarget = on ? 0.0 : 1.0;
        m_blackSpeed = seconds > 0.0 ? 1.0 / seconds : 0.0;
        m_blackoutClock.reset(RealtimeTiming::nowNs());
        if (m_blackSpeed <= 0.0) m_blackLevel = m_blackTarget;
    }
    m_audio->fadeTo(on ? 0.0f : 1.0f, seconds);
}

bool Engine::blackout() const
{
    Lock lk(&m_mutex);
    return m_blackTarget < 0.5;
}

void Engine::setBlackoutFade(double seconds)
{
    Lock lk(&m_mutex);
    m_blackFade = std::clamp(seconds, 0.0, 60.0);
}

double Engine::blackoutFade() const
{
    Lock lk(&m_mutex);
    return m_blackFade;
}

void Engine::requestSourcePreview(quint64 layerId, int maxSide)
{
    Lock lk(&m_mutex);
    maxSide = std::clamp(maxSide, 16, 2048);
    if (m_previewId != layerId || m_previewSide != maxSide) m_previewNextNs = 0;
    m_previewId = layerId;
    m_previewSide = maxSide;
}

QImage Engine::sourcePreview(quint64 *layerId) const
{
    std::lock_guard<std::mutex> lk(m_previewMutex);
    if (layerId) *layerId = m_previewImageId;
    return m_previewImage;
}

// ---------------------------------------------------------------------------
// Rendering settings
// ---------------------------------------------------------------------------
void Engine::setRenderSettings(const RenderSettings &r)
{
    Lock lk(&m_mutex);
    m_render = r;
}

Engine::RenderSettings Engine::renderSettings() const
{
    Lock lk(&m_mutex);
    return m_render;
}

void Engine::setRenderDefaults(const RenderSettings &r)
{
    Lock lk(&m_mutex);
    m_renderDefaults = r;
}

Engine::RenderSettings Engine::renderDefaults() const
{
    Lock lk(&m_mutex);
    return m_renderDefaults;
}

Engine::RenderSettings Engine::effectiveRender() const
{
    Lock lk(&m_mutex);
    RenderSettings e;
    e.frameRate = (m_render.frameRate >= 0 || m_render.frameRate == RenderSettings::UncappedFrameRate) ? m_render.frameRate : std::max(0.0, m_renderDefaults.frameRate);
    e.samples = m_render.samples >= 0 ? m_render.samples : std::max(0, m_renderDefaults.samples);
    e.mipmaps = m_render.mipmaps >= 0 ? m_render.mipmaps : std::max(0, m_renderDefaults.mipmaps);
    // RGBA16 UNORM ("10") unless 8 bits are chosen: smooth generated gradients by default, half the GPU memory and
    // bandwidth on request (heavy 4K / 8K shows)
    e.depth = m_render.depth > 0 ? m_render.depth : (m_renderDefaults.depth > 0 ? m_renderDefaults.depth : 10);
    return e;
}

void Engine::setScreenRefreshRate(double hz)
{
    Lock lk(&m_mutex);
    m_screenHz = hz > 1 ? hz : 60.0;
}

double Engine::screenRefreshRate() const
{
    Lock lk(&m_mutex);
    return m_screenHz;
}

// ---------------------------------------------------------------------------
// Resources against their budgets (Engine::resourcesJson)
// ---------------------------------------------------------------------------
void Engine::sampleResources()
{
    const qint64 now = FrameTiming::nowNs();
    if (m_resourcesSampledNs && now - m_resourcesSampledNs < 1000000000) return;
    m_resourcesSampledNs = now;
    { // The output windows, for frameTimingJson (render thread: m_outWindows is its own)
        std::vector<OutputStats> outs;
        Lock lk(&m_mutex);
        for (const auto &[id, o] : m_outWindows) {
            OutputStats st;
            st.viewport = id;
            const int i = indexOfId(id);
            if (i >= 0) st.name = m_layers[size_t(i)]->name;
            st.exposed = o.exposed;
            st.hz = o.hz;
            st.presented = o.presented;
            st.noDrawable = o.noDrawable;
            st.surfacesBusy = o.surfacesBusy;
            st.failed = o.failed;
            st.noPicture = o.noPicture;
            st.notRenderThread = o.notRenderThread;
            outs.push_back(st);
        }
        std::lock_guard<std::mutex> g(m_outputStatsMutex);
        m_outputStats = std::move(outs);
    }
    quint32 over = 0;
    if (VideoDecoder::decodedBytesInUse() > VideoDecoder::decodedBytesLimit()) over |= 1;
    if (VideoDecoder::softwareThreadsInUse() > VideoDecoder::softwareThreadBudgetNow()) over |= 4;
    const qint64 freeKb = m_vramFreeKb.load(), totalKb = m_vramTotalKb.load();
    if (freeKb >= 0 && ((totalKb > 0 && freeKb * 10 < totalKb) || (totalKb <= 0 && freeKb < 256 * 1024))) over |= 8;
    const quint32 before = m_overBudget.exchange(over);
    static const char *const what[] = {"decoded frames over their memory budget (each stream keeps 2 ahead)",
                                       "upload buffers over their budget (its limit was lowered: the extra ones go as they come back)",
                                       "software decoders' threads over the budget (new ones share it)",
                                       "graphics memory nearly full"};
    for (int k = 0; k < 4; ++k)
        if ((over & ~before) >> k & 1u) qWarning("Resources: %s", what[k]);
}

QJsonObject Engine::resourcesJson() const
{
    auto mb = [](size_t bytes) { return std::round(double(bytes) / (1024.0 * 1024.0) * 10.0) / 10.0; };
    QJsonObject o;
    o["decoded_frames_mb"] = mb(VideoDecoder::decodedBytesInUse());
    o["decoded_frames_budget_mb"] = mb(VideoDecoder::decodedBytesLimit());
    o["video_memory_mb"] = mb(videoBudget::memoryInUse());
    o["video_memory_budget_mb"] = mb(videoBudget::memoryLimit());
    o["software_decoders"] = VideoDecoder::softwareDecodersOpen();
    o["software_decode_threads"] = VideoDecoder::softwareThreadsInUse();
    o["software_decode_thread_budget"] = VideoDecoder::softwareThreadBudgetNow();
    o["hap_workers"] = workers::threadCount();
    o["ffmpeg_errors_total"] = double(VideoDecoder::ffmpegErrorsTotal());
    o["empty_video_loops_stopped_total"] = double(VideoDecoder::emptyLoopStopsTotal());
    // What NyxMap allocated on the GPU (estimated from sizes and formats; the driver's own figures below)
    o["graphics_memory_allocated_mb"] = mb(videoBudget::gpuInUse());
    o["graphics_memory_render_targets_mb"] = mb(videoBudget::gpuInUse(videoBudget::Gpu::Targets));
    o["graphics_memory_images_mb"] = mb(videoBudget::gpuInUse(videoBudget::Gpu::Images));
    o["graphics_memory_video_planes_mb"] = mb(videoBudget::gpuInUse(videoBudget::Gpu::VideoPlanes));
    o["graphics_memory_video_surfaces_mb"] = mb(videoBudget::gpuInUse(videoBudget::Gpu::Surfaces));
    if (m_vramFreeKb.load() >= 0) o["graphics_memory_free_mb"] = double(m_vramFreeKb.load()) / 1024.0;
    if (m_vramTotalKb.load() > 0) o["graphics_memory_total_mb"] = double(m_vramTotalKb.load()) / 1024.0;
    if (const uint64_t rss = processMemory::residentBytes()) o["process_resident_mb"] = mb(size_t(rss)); // all of it
    QJsonArray warnings;
    static const char *const keys[] = {"decoded_frames", "upload_buffers", "decode_threads", "graphics_memory"};
    const quint32 over = m_overBudget.load();
    for (int k = 0; k < 4; ++k)
        if (over >> k & 1u) warnings.append(QString::fromLatin1(keys[k]));
    o["warnings"] = warnings;
    return o;
}
