// The QRhi renderer's video pictures (RhiVideoTexture) against the OpenGL renderer's (VideoTexture), file by file:
// hardware frames taken on the GPU (macOS: no copy; Windows: decoded on QRhi's D3D11 device, one GPU copy), planes
// uploaded from the CPU (every pixel layout), HAP's compressed blocks.
//  - pictures: the first frame through both must match;
//  - cost: each frame uploaded (or taken) and converted, GPU done: median, P95, P99, through each.
// The two run one after the other (on Windows the decoders decode on QRhi's device for QRhi, on their own for
// OpenGL), each from a fresh decoder.
//
// nyxmap_rhi_video_test [--frames N] [video files...]
//   (default: test/media h264.mp4, the HAP and the pixel layout files there). Exit 77 (skipped) without a QRhi
//   backend or an OpenGL context.
#include "Gl.h"
#include "RhiDevice.h"
#include "RhiVideoSurface.h"
#include "RhiVideoTexture.h"
#include "VideoDecoder.h"
#include "VideoTexture.h"

extern "C" {
#include <libavutil/buffer.h>
}

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

static int g_failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                 \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

struct Stats {
    std::vector<double> ms;
    double at(double q) const
    {
        if (ms.empty()) return 0;
        std::vector<double> s = ms;
        std::sort(s.begin(), s.end());
        return s[std::min(s.size() - 1, size_t(q * double(s.size())))];
    }
};

struct Result {
    QImage first; // top row first
    QString description;
    Stats ms;
    uint64_t taken = 0; // frames taken on the GPU
    int frames = 0;
    bool ok = false;
};

// Plays `path` from its start and hands each frame to `show` (true: shown); `show` times itself
template <class Show>
static bool play(const QString &path, int frames, Show show)
{
    VideoDecoder dec;
    QString err;
    if (!dec.open(path, &err)) {
        std::printf("FAIL %s: %s\n", qPrintable(path), qPrintable(err));
        ++g_failures;
        return false;
    }
    Timeline t;
    t.mode = Timeline::Loop;
    dec.setTimeline(t);
    dec.seek(0);
    VideoFrame fr;
    int got = 0;
    QElapsedTimer wall;
    wall.start();
    for (double clock = 0.0005; got < frames && wall.elapsed() < 30000; clock += 1.0 / std::max(1.0, dec.fps())) {
        while (!dec.fetch(clock, fr) && wall.elapsed() < 30000) QThread::usleep(200);
        if (!fr.layout) continue;
        if (!show(fr, dec, got)) return false;
        ++got;
    }
    return got > 0;
}

static Result viaRhi(QRhi *rhi, const QString &path, int frames)
{
    Result r;
    RhiVideoTexture tex(rhi);
    r.ok = play(path, frames, [&](VideoFrame &fr, VideoDecoder &, int k) {
        QRhiCommandBuffer *cb = nullptr;
        QElapsedTimer tm;
        tm.start();
        if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return false;
        const bool shown = tex.upload(cb, fr);
        QRhiReadbackResult rb;
        if (shown && k == 0) {
            QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
            u->readBackTexture({tex.texture()}, &rb);
            cb->resourceUpdate(u);
        }
        if (rhi->endOffscreenFrame() != QRhi::FrameOpSuccess || !shown) {
            std::printf("  %s %s: not converted through QRhi\n", qPrintable(QFileInfo(path).fileName()),
                        qPrintable(fr.layout->description));
            return false;
        }
        if (k == 0) {
            r.description = fr.layout->description;
            r.first = QImage(reinterpret_cast<const uchar *>(rb.data.constData()), rb.pixelSize.width(), rb.pixelSize.height(),
                             QImage::Format_RGBA8888)
                          .mirrored(false, true); // stored bottom row first
        } else {
            r.ms.ms.push_back(tm.nsecsElapsed() / 1e6); // the first builds the pipeline and reads back: left out
        }
        r.frames = k + 1;
        return true;
    });
    r.taken = tex.surfaceUploads();
    return r;
}

// An OpenGL texture's pixels, top row first (VideoTexture's output is drawn bottom row first)
static QImage readTexture(GLuint tex, int w, int h)
{
    auto f = gl();
    GLuint fbo = 0;
    f->glGenFramebuffers(1, &fbo);
    f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    QImage img(w, h, QImage::Format_RGBA8888);
    f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    f->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    f->glDeleteFramebuffers(1, &fbo);
    return img.mirrored(false, true);
}

static Result viaOpenGl(const VideoConverter &conv, const QString &path, int frames)
{
    Result r;
    VideoTexture tex;
    r.ok = play(path, frames, [&](VideoFrame &fr, VideoDecoder &dec, int k) {
        QElapsedTimer tm;
        tm.start();
        tex.feed(dec);
        tex.upload(fr, dec, conv);
        gl()->glFinish();
        if (k == 0) {
            r.description = fr.layout->description;
            r.first = readTexture(tex.texture(), tex.width(), tex.height());
        } else {
            r.ms.ms.push_back(tm.nsecsElapsed() / 1e6);
        }
        r.frames = k + 1;
        return true;
    });
    r.taken = tex.surfaceUploads();
    tex.destroy();
    return r;
}

struct Diff {
    int max = 0;
    double mean = 0;
    bool sameSize = false;
};
static Diff compare(const QImage &a, const QImage &b)
{
    Diff d;
    d.sameSize = a.size() == b.size() && !a.isNull();
    if (!d.sameSize) return d;
    double sum = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar *p = a.constScanLine(y), *q = b.constScanLine(y);
        for (int x = 0; x < a.width() * 4; ++x) {
            const int v = std::abs(int(p[x]) - int(q[x]));
            d.max = std::max(d.max, v);
            sum += v;
        }
    }
    d.mean = sum / (double(a.width()) * a.height() * 4);
    return d;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    int frames = 30;
    if (const qsizetype i = args.indexOf(QStringLiteral("--frames")); i >= 0 && i + 1 < args.size()) {
        frames = std::max(2, args.takeAt(i + 1).toInt());
        args.removeAt(i);
    }
    if (args.isEmpty()) {
        const QDir media(QStringLiteral(TEST_DIR "/media"));
        for (const QString &f : {QStringLiteral("h264.mp4"), QStringLiteral("hap1.mov"), QStringLiteral("hap5.mov"),
                                 QStringLiteral("hapy.mov")})
            if (media.exists(f)) args << media.filePath(f);
        for (const QString &f : media.entryList({QStringLiteral("fmt_*")}, QDir::Files)) args << media.filePath(f);
    }

    QString backend;
    std::unique_ptr<QRhi> rhi = rhidevice::create({}, &backend);
    if (!rhi) {
        std::printf("SKIP: no QRhi backend here\n");
        return 77;
    }
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    QOffscreenSurface surface;
    surface.setFormat(fmt);
    surface.create();
    if (!ctx.create() || !ctx.makeCurrent(&surface)) {
        std::printf("SKIP: no OpenGL context\n");
        return 77;
    }
    auto f = gl();
    const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    GLuint vao = 0, vbo = 0;
    f->glGenVertexArrays(1, &vao);
    f->glBindVertexArray(vao);
    f->glGenBuffers(1, &vbo);
    f->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    f->glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    f->glBindVertexArray(0);
    VideoConverter conv;
    QString err;
    if (!conv.init(vao, &err)) {
        std::printf("FAIL: OpenGL conversion: %s\n", qPrintable(err));
        return 1;
    }
    std::printf("QRhi %s, hardware frames taken on the GPU: %s\n", qPrintable(backend),
                RhiVideoSurface::available(rhi.get()) ? "yes" : "no");

    for (const QString &path : args) {
        const QString name = QFileInfo(path).fileName();
        // QRhi: the decoders on its device where it shares one (Windows), hardware frames taken on the GPU
        AVBufferRef *device = RhiVideoSurface::decoderDevice(rhi.get());
        VideoDecoder::setSharedHardwareDevice(device);
        av_buffer_unref(&device);
        VideoDecoder::setZeroCopy(RhiVideoSurface::available(rhi.get()));
        VideoDecoder::setGpuFormats(rhi->isTextureFormatSupported(QRhiTexture::BC1), rhi->isTextureFormatSupported(QRhiTexture::BC7));
        const Result q = viaRhi(rhi.get(), path, frames);
        // OpenGL: as the OpenGL renderer sets it
        VideoDecoder::setSharedHardwareDevice(nullptr);
        VideoDecoder::setZeroCopy(conv.surfaces());
        VideoDecoder::setGpuFormats(conv.s3tc(), conv.bptc());
        const Result g = viaOpenGl(conv, path, frames);
        if (!q.ok || !g.ok) {
            std::printf("FAIL %s: %s\n", qPrintable(name), !q.ok ? "QRhi" : "OpenGL");
            ++g_failures;
            continue;
        }
        const Diff d = compare(q.first, g.first);
        std::printf("  %-20s %-38s QRhi against OpenGL: max %3d mean %.3f\n", qPrintable(name), qPrintable(q.description), d.max,
                    d.mean);
        CHECK(d.sameSize && d.max <= 2 && d.mean <= 0.5); // rounding: 16-bit planes filtered at another precision
        std::printf("      %d frames, GPU done: QRhi median %.2f ms P95 %.2f P99 %.2f (%llu on the GPU) · OpenGL median %.2f ms "
                    "P95 %.2f P99 %.2f (%llu on the GPU)\n",
                    q.frames, q.ms.at(0.5), q.ms.at(0.95), q.ms.at(0.99), (unsigned long long)q.taken, g.ms.at(0.5), g.ms.at(0.95),
                    g.ms.at(0.99), (unsigned long long)g.taken);
    }
    VideoDecoder::setSharedHardwareDevice(nullptr);
    conv.release();
    std::printf(g_failures ? "%d failure(s)\n" : "ok\n", g_failures);
    return g_failures ? 1 : 0;
}
