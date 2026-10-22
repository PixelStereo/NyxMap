#pragma once
// Video decoding on a dedicated thread.
// Frames are kept as the decoder gives them (YUV planes, RGB, grey) or, for HAP, as their compressed textures:
// the GPU converts them (VideoTexture). They are queued, stamped with the clock of the layer's Timeline
// (monotonic whatever the direction or the play mode).
// Backward legs (negative speed, ping-pong) are produced in short windows: seek to the keyframe, decode
// forward, deliver the frames in reverse order — bounded memory whatever the GOP length.
//
// Upload buffers: the render thread hands the decoder mapped pixel buffers (addStaging); the decoder copies
// each frame into one of them as soon as it is decoded, so the render thread has no copy of its own to make.
// Without a free one, the frame stays in the decoder's memory and is uploaded from there.
//
// Hardware decoding (VideoToolbox on macOS, D3D11VA / DXVA2 on Windows, VAAPI on Linux) when the codec allows
// it; software otherwise, or when the hardware refuses a stream. On macOS the GPU samples VideoToolbox's frames
// where they were decoded (zero copy: no upload buffer, no copy on the CPU); elsewhere they are copied back to
// memory, then uploaded.

#include "Timeline.h"
#include "VideoFrame.h"

#include <QString>
#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct AVBufferRef;
struct SwsContext;

// How a video source is read besides its path (saved with the layer):
//  - an image sequence: numbered still pictures in a folder (the path), read as a clip by FFmpeg's image2 demuxer
//    from `pattern` (a file name, printf style: "shot_%04d.exr"), from number `start`, at `fps` frames per second.
//    image2 reads a contiguous range: the first gap in the numbering ends the clip.
//  - a live network stream (the path is its URL: rtsp, rtmp, srt, udp, rtp, http(s), HLS…): no duration, no seeking,
//    the newest picture shown; the decoder connects on its own thread and connects again when the stream drops.
struct VideoSource {
    QString pattern;
    int start = 0;
    double fps = 25;
    bool live = false;
    bool rtspTcp = true; // RTSP over TCP (interleaved): through firewalls, no lost packets; off: UDP, lower latency
    bool isSequence() const { return !pattern.isEmpty(); }
    bool operator==(const VideoSource &o) const
    {
        return pattern == o.pattern && start == o.start && fps == o.fps && live == o.live && rtspTcp == o.rtspTcp;
    }
    bool operator!=(const VideoSource &o) const { return !(*this == o); }
};

class VideoDecoder
{
public:
    VideoDecoder() = default;
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder &) = delete;
    VideoDecoder &operator=(const VideoDecoder &) = delete;

    // A file, an image sequence (path: its folder) or a live stream (path: its URL). A live stream is not connected
    // here: open() returns at once and the decode thread connects (liveStatus()).
    bool open(const QString &path, QString *err, const VideoSource &source = VideoSource());

    // File info without decoding it (media bin)
    struct Info {
        int width = 0, height = 0;
        int frames = 0; // an image sequence's pictures (0: not one)
        double duration = 0, fps = 0;
        QString codec;
        QString pixelFormat;    // container's declared AVPixelFormat (may be unspecified)
        int bitDepth = 0;       // actual source component bits, 0: unknown
        QString matrix;         // YUV matrix coefficients, not the ICC profile
        QString primaries;      // color primaries
        QString transfer;       // EOTF / gamma tag
        QString range;          // full / limited
        QString colorDescription() const;
        QString colorSpaceSummary() const;
    };
    // Never for a live stream (it would connect): false
    static bool probe(const QString &path, Info *info, QString *err = nullptr, const VideoSource &source = VideoSource());
    void close();

    int width() const { return m_width; }
    int height() const { return m_height; }
    double duration() const { return m_duration; } // 0 for a live stream
    double fps() const;
    // "h264", "prores", "Hap Q"…, followed by the hardware decoder once it delivers frames
    QString codecName() const;
    Info sourceInfo() const; // captured before decoder starts (a live stream: at each connection)

    // --- Live streams
    bool isLive() const { return m_live; }
    enum class LiveState { Connecting, Live, Reconnecting };
    struct LiveStatus {
        LiveState state = LiveState::Connecting;
        QString reason;   // why it is (re)connecting: the last error
        int attempts = 0; // connections tried since the last one that worked
    };
    LiveStatus liveStatus() const;
    static constexpr int kLiveTimeoutUs = 5000000; // no data for this long: the stream is taken as dropped
    bool isHap() const { return m_hapTag != 0; }
    bool hardwareActive() const { return m_hwActive.load(); }
    int decodeThreads() const; // FFmpeg's threads for this video (0: HAP, decoded here)

    // Timeline followed by the next seek (mode, origin, direction). Its duration is set by the decoder.
    void setTimeline(const Timeline &t);

    // Repositions on the clock `c` of the timeline (asynchronous): frames are then stamped from there.
    void seek(double c);

    // Fetches the most recent frame whose timestamp <= t into `out`. Returns true if it is a new one.
    // What `out` held before goes back to the decoder, its upload buffer included unless the render thread
    // took that one back (out.staging < 0).
    bool fetch(double t, VideoFrame &out);
    // The same as RGBA 8 bits, bottom row first (tests and tools; converted on the CPU)
    bool fetchRgba(double t, std::vector<uint8_t> &out, int *w, int *h);

    // True when non-looping playback has reached the end and the queue is empty.
    bool finished();

    // Lightweight real-time diagnostics. Counters are lock-free so the render thread can sample them.
    // p95DecodeMs is an approximate percentile from fixed buckets (no per-frame allocations).
    struct Metrics {
        uint64_t decodedFrames = 0;
        uint64_t fetchedFrames = 0;
        uint64_t skippedFrames = 0;   // queued frames made obsolete by a later fetch
        uint64_t fetchMisses = 0;     // no frame available; caller may have to repeat the previous frame
        uint64_t lateFrames = 0;      // fetched frame lag exceeded 1.5 source-frame intervals
        uint64_t budgetHolds = 0;     // times the stream kept fewer frames ahead: decoded frames over the memory budget
        uint64_t queueHighWatermark = 0;
        double decodeTimeTotalMs = 0;
        double decodeTimeMaxMs = 0;
        double decodeTimeP95Ms = 0;   // bucket estimates (upper bound of the bucket, at most the max), not exact
        double decodeTimeP99Ms = 0;
        uint64_t decodesOverFrame = 0; // decodes longer than one frame of the file (1 / fps): a deadline missed
        double lastFrameLatenessMs = 0;
        double maxFrameLatenessMs = 0;
        uint64_t demuxErrors = 0, codecErrors = 0, emptyLoopStops = 0, ioRetries = 0;
        int lastAvError = 0; // last non-EOF FFmpeg error code, 0 if none
    };
    Metrics metrics() const;
    static uint64_t ffmpegErrorsTotal(); // process-wide counters for endurance telemetry
    static uint64_t emptyLoopStopsTotal();
    // An FFmpeg codec error that makes the decoder unusable (EINVAL, ENOMEM); any other is one damaged packet
    static bool isFatalCodecError(int avError);
    static constexpr int kMaxConsecutiveCodecErrors = 64; // then the stream is taken as unplayable
    static constexpr int kMaxIoRetries = 5;               // transient demux errors retried, with a growing pause
    void resetMetrics();
    void recordDecodeTime(uint64_t nanoseconds); // decode thread (and tests)

    // --- Upload buffers (render thread)
    size_t stagingSize() const { return m_stagingSize.load(); } // bytes of one frame (0: not known yet)
    int stagingCount() const;                                   // how many buffers it can use
    void addStaging(int id, uint8_t *ptr, size_t size);         // a mapped buffer, free to be written
    std::vector<int> takeFreeStaging();                         // the free ones, given back (size change)
    std::vector<int> takeStaleStaging(size_t size);             // the free ones of another size, given back

    // --- Settings, for the decoders opened afterwards
    static void setHardwareDecoding(bool on);
    static bool hardwareDecoding();
    // Software decoders' FFmpeg threads across the process (see softwareThreads in VideoDecoder.cpp)
    static int softwareThreadsInUse();
    static int softwareDecodersOpen();
    static int softwareThreadBudgetNow();         // threads they may take in all before sharing evenly
    static void setSoftwareThreadBudget(int threads); // 0: the default (twice the cores), tests and measurements
    // Decoded frames in memory, across every decoder, and their soft limit (bytes)
    static size_t decodedBytesInUse();
    static size_t decodedBytesLimit();
    // Hardware frames handed to the GPU in place (set by the render thread when it can sample them: macOS)
    static void setZeroCopy(bool on);
    static bool zeroCopy();
    // A hardware frame whose surface the GPU refused: frames of its format are copied from now on (true: newly)
    static bool refuseSurface(const AVFrame *hw);
    // Compressed texture formats the GPU samples (HAP): DXT (S3TC) and BPTC (BC7, BC6). RGTC is always there.
    static void setGpuFormats(bool s3tc, bool bptc);

    // A frame as RGBA 8 bits on the CPU (not one held in an upload buffer). False if it cannot be read.
    static bool toRgba(const VideoFrame &f, std::vector<uint8_t> &out, bool bottomUp);
    // How the GPU reads a decoded picture of this format (null: converted on the CPU instead)
    static std::shared_ptr<const VideoLayout> layoutOf(const AVFrame *f);

private:
    bool openStreams(const QString &path, const VideoSource &source, QString *err); // demuxer and decoder
    void releaseStreams();                                                         // what openStreams made
    void runLive();                              // decode thread of a live stream: connects, decodes, reconnects
    void setLiveState(LiveState s, const QString &reason, int attempts);
    static int interruptCallback(void *self);    // FFmpeg's blocking calls give up when the decoder closes
    void run();
    void runLoop(); // run(): restarted after an allocation failure, which stops playback until the next seek
    bool doSeek(double t);
    int decodeNext(VideoFrame &f, double skipBefore = -1e9); // 1 = frame, 2 = skipped, 0 = end, -1 = error,
                                                             // 3 = transient I/O error (retry after a pause)
    int readVideoPacket(); // demux next packet from the selected video stream
    int decodeHap(VideoFrame &f, double skipBefore);
    bool deliver(AVFrame *src, VideoFrame &f, bool mapped); // decoded picture -> f (upload buffer, or kept)
    void produceBackwardWindow(); // decode thread, backward leg
    void startLeg(const Timeline::Leg &leg, double position); // decode thread
    void finishLeg();                                          // decode thread: next leg, or end
    void markDecodeError(int code, bool demux, bool fatal); // never treats I/O failure as EOF
    bool codecFailure(int code); // counts a codec error; true: playback stops
    int ioFailure(int code);     // counts a demux error; 3 (retry after a pause) or -1 (retries spent)
    void clearIoError();         // the demuxer's sticky read error, cleared before a retry
    void resumeAfterIoError();   // forward leg: demuxer error cleared, seek back to after the last frame queued
    void recycle(VideoFrame &&f);       // takes the lock
    void recycleLocked(VideoFrame &&f); // lock held
    void takePooledBytes(VideoFrame &f); // takes the lock
    bool takeStaging(size_t size, int *id, uint8_t **ptr);
    std::shared_ptr<const VideoLayout> layoutFor(const AVFrame *f);
    std::shared_ptr<const VideoLayout> surfaceLayout(const AVFrame *hw); // null: not sampled in place
    std::shared_ptr<const VideoLayout> hapLayout(int count, int f0, int f1);
    bool setupHardware(const struct AVCodec *dec);
    friend struct HwFormat;

    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_codec = nullptr;
    AVFrame *m_frame = nullptr, *m_swFrame = nullptr;
    AVPacket *m_packet = nullptr;
    SwsContext *m_sws = nullptr;
    AVBufferRef *m_hwDevice = nullptr;
    int m_hwPixFmt = -1;
    QString m_hwName;
    std::atomic<bool> m_hwActive{false};
    int m_stream = -1;
    double m_timeBase = 0, m_startTime = 0;

    std::atomic<int> m_width{0}, m_height{0}; // written by the decode thread of a live stream
    int m_softwareThreads = 0; // FFmpeg threads counted in the process-wide budget (0: hardware, HAP, closed)
    double m_duration = 0, m_fps = 25;
    QString m_codecName;
    Info m_sourceInfo; // immutable tagged source colorimetry/precision, independent of decoder output
    uint32_t m_hapTag = 0;
    std::vector<uint8_t> m_hapScratch; // HAP textures decoded on the CPU: their blocks, decompressed

    // Layouts already made (decode thread)
    struct LayoutKey {
        int format, w, h, space, range, loc, primaries = 0, transfer = 0;
        bool operator<(const LayoutKey &o) const
        {
            return std::tie(format, w, h, space, range, loc, primaries, transfer) <
                   std::tie(o.format, o.w, o.h, o.space, o.range, o.loc, o.primaries, o.transfer);
        }
    };
    std::map<LayoutKey, std::shared_ptr<const VideoLayout>> m_layouts;
    std::map<const VideoLayout *, std::shared_ptr<const VideoLayout>> m_surfaceLayouts; // by the copy path's layout
    std::shared_ptr<const VideoLayout> m_rgbaLayout; // CPU conversion of the formats the GPU does not take

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<VideoFrame> m_queue;
    std::vector<std::vector<uint8_t>> m_bytePool;
    std::vector<size_t> m_bytePoolBudgetBytes; // reservations transferred with pooled allocations
    struct Staging {
        int id = -1;
        uint8_t *ptr = nullptr;
        size_t size = 0;
    };
    std::vector<Staging> m_freeStaging;
    std::map<int, Staging> m_stagingById; // every buffer handed over, wherever it is
    std::atomic<size_t> m_stagingSize{0};

    // Metrics use atomics because decode and render/fetch run on different threads.
    std::atomic<uint64_t> m_metricDecoded{0}, m_metricFetched{0}, m_metricSkipped{0}, m_metricMisses{0};
    std::atomic<uint64_t> m_metricLate{0}, m_metricBudgetHolds{0}, m_metricQueueHigh{0};
    std::atomic<uint64_t> m_metricDecodeNsTotal{0}, m_metricDecodeNsMax{0};
    std::atomic<uint64_t> m_metricLastLateNs{0}, m_metricMaxLateNs{0};
    // Decode times by bucket: up to 0.25, 0.5, 1, 2, 4, 8, 16, 33, 50, 100, 250, 500, 1000, 2000 ms, then beyond
    static constexpr size_t kDecodeBuckets = 15;
    static constexpr uint64_t kDecodeBucketNs[kDecodeBuckets - 1] = {
        250000, 500000, 1000000, 2000000, 4000000, 8000000, 16000000, 33000000,
        50000000, 100000000, 250000000, 500000000, 1000000000, 2000000000};
    std::array<std::atomic<uint64_t>, kDecodeBuckets> m_metricDecodeBuckets{};
    std::atomic<uint64_t> m_metricOverFrame{0};
    std::atomic<uint64_t> m_metricDemuxErrors{0}, m_metricCodecErrors{0}, m_metricEmptyLoopStops{0}, m_metricIoRetries{0};
    std::atomic<int> m_lastAvError{0};
    void recordQueueDepth(size_t depth);
    void accountFrame(VideoFrame &frame); // counted in the process-wide budget of decoded frames

    bool m_quit = false, m_seekPending = false, m_eof = false, m_needFirst = true;
    bool m_seekFailed = false; // protected by m_mutex; never reported as clean EOF
    double m_seekTarget = 0;
    uint64_t m_generation = 0;
    Timeline m_timeline, m_pendingTimeline; // pending: applied at the next seek

    // Decode thread state
    bool m_draining = false;
    double m_lastPts = 0, m_discardBefore = -1e9;
    Timeline::Leg m_leg;                // leg being produced
    std::vector<VideoFrame> m_backStack; // backward leg: frames still to deliver (reverse order)
    double m_backEnd = 0;               // backward leg: end (position) of the next window
    bool m_legDone = false;
    uint64_t m_legFrames = 0; // a Loop/PingPong leg with no frames must never spin forever
    int m_emptyLegs = 0;      // empty legs in a row
    bool m_resendPacket = false; // send_packet said EAGAIN: m_packet kept, sent again after the output is read
    int m_consecutiveCodecErrors = 0, m_ioRetries = 0;
    double m_resumeSeek = 0, m_resumeDiscard = -1e9; // where a transient I/O error resumes the forward leg

    // Live stream: set by open() before the decode thread starts, read-only afterwards
    bool m_live = false;
    QString m_livePath;
    VideoSource m_liveSource;
    std::atomic<bool> m_abort{false}; // close(): FFmpeg's blocking calls (connecting, reading) return
    // What the decode thread found at its last connection, and the state (other threads read them)
    mutable std::mutex m_infoMutex; // m_sourceInfo and m_liveCodecName too, for a live stream
    LiveStatus m_liveStatus;
    QString m_liveCodecName;
};
