// QRhi prototype, video interop (docs/developers/qrhi-prototype.md), macOS: VideoToolbox's frames reach QRhi's
// Metal textures with no copy (CVMetalTextureCache over their IOSurfaces), and one draw turns them into RGBA with
// the formula the OpenGL path takes (VideoLayout: channels, bit depth, chroma siting, matrix, range).
//  - colours: the first frame against FFmpeg's own decoding (the tolerances of the OpenGL path's test);
//  - cost: the same frames through QRhi (import, convert, GPU done) and through OpenGL (VideoTexture::upload,
//    glFinish), median, P95, P99.
//
// nyxmap_rhi_video_test [--frames N] [video files...]   (default: test/media/h264.mp4, 60 frames)
// Exit 77 (skipped) without a Metal device or hardware frames.
#include "Gl.h"
#include "VideoBudget.h"
#include "VideoDecoder.h"
#include "VideoTexture.h"
#include "../test/ffmpeg_reference.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QThread>
#include <rhi/qrhi.h>
#include <rhi/qshaderbaker.h>

#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>

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

static const char *kVertex = R"(#version 440
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0; // a triangle over the target
    v_uv = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

// VideoTexture's conversion (YUV, RGB, grey), each output channel from its own binding
static const char *kFragment = R"(#version 440
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform Conversion {
    vec4 sel0, sel1, sel2;
    vec4 off01, off2;  // chroma siting of channels 0 and 1, 2
    vec4 scale;        // bit depth held in 16 bits
    vec4 m0, m1, m2;   // the YUV matrix, by rows
    vec4 range;        // Y offset, Y scale, C offset, C scale
    vec4 crop;         // part of the textures holding the picture, mode (0 YUV, 1 RGB, 2 grey), flip
};
layout(binding = 1) uniform sampler2D c0;
layout(binding = 2) uniform sampler2D c1;
layout(binding = 3) uniform sampler2D c2;
void main() {
    vec2 uv = vec2(v_uv.x, crop.w > 0.5 ? 1.0 - v_uv.y : v_uv.y) * crop.xy; // stored top row first
    float a = dot(texture(c0, uv + off01.xy), sel0) * scale.x;
    float b = dot(texture(c1, uv + off01.zw), sel1) * scale.y;
    float c = dot(texture(c2, uv + off2.xy), sel2) * scale.z;
    vec3 v = vec3((a - range.x) * range.y, (b - range.z) * range.w, (c - range.z) * range.w);
    vec3 rgb = crop.z < 0.5 ? vec3(dot(m0.xyz, v), dot(m1.xyz, v), dot(m2.xyz, v)) : crop.z < 1.5 ? vec3(a, b, c) : vec3(a);
    o = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

static QShader bake(const char *src, QShader::Stage stage)
{
    QShaderBaker b;
    b.setSourceString(QByteArray(src), stage);
    b.setGeneratedShaders({{QShader::MslShader, QShaderVersion(20)}});
    b.setGeneratedShaderVariants({QShader::StandardShader});
    const QShader s = b.bake();
    if (!s.isValid()) std::printf("shader: %s\n", qPrintable(b.errorMessage()));
    return s;
}

struct Stats {
    std::vector<double> ms;
    void add(double v) { ms.push_back(v); }
    double at(double q) const
    {
        if (ms.empty()) return 0;
        std::vector<double> s = ms;
        std::sort(s.begin(), s.end());
        return s[std::min(s.size() - 1, size_t(q * double(s.size())))];
    }
};

// The QRhi side: an offscreen Metal QRhi, the conversion pipeline, a texture cache over the decoder's surfaces
class RhiPath
{
public:
    bool init()
    {
        QRhiMetalInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Metal, &params));
        if (!m_rhi) return false;
        auto *handles = static_cast<const QRhiMetalNativeHandles *>(m_rhi->nativeHandles());
        m_device = (id<MTLDevice>)handles->dev;
        CVMetalTextureCacheRef cache = nullptr;
        if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, m_device, nullptr, &cache) != kCVReturnSuccess) return false;
        m_cache = cache;
        m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16 * 4 * 12));
        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        return m_ubuf->create() && m_sampler->create();
    }
    ~RhiPath()
    {
        m_ps.reset();
        m_srb.reset();
        m_rp.reset();
        m_rt.reset();
        m_out.reset();
        m_planes.clear();
        for (CVMetalTextureRef t : m_held) CFRelease(t);
        m_held.clear();
        m_sampler.reset();
        m_ubuf.reset();
        if (m_cache) {
            CVMetalTextureCacheFlush(m_cache, 0);
            CFRelease(m_cache);
        }
        m_rhi.reset();
    }

    // Converts a hardware frame into the RGBA target; readback: its pixels, top row first (nullptr: not read)
    bool convert(const VideoFrame &f, QImage *readback)
    {
        const VideoLayout &L = *f.layout;
        auto pixels = reinterpret_cast<CVPixelBufferRef>(f.av->data[3]);
        if (!pixels || !ensureTarget(L.width, L.height)) return false;
        // Each plane of the surface as a Metal texture, adopted by QRhi: no copy
        std::vector<CVMetalTextureRef> held;
        std::vector<std::unique_ptr<QRhiTexture>> planes;
        for (int p = 0; p < L.planeCount; ++p) {
            const VideoLayout::Plane &pl = L.plane[p];
            const bool wide = pl.texels == VideoLayout::Texels::U16;
            if (pl.channels > 2 || (pl.texels != VideoLayout::Texels::U8 && !wide)) return false; // not in this probe
            const MTLPixelFormat mf = pl.channels == 1 ? (wide ? MTLPixelFormatR16Unorm : MTLPixelFormatR8Unorm)
                                                       : (wide ? MTLPixelFormatRG16Unorm : MTLPixelFormatRG8Unorm);
            const QRhiTexture::Format qf = pl.channels == 1 ? (wide ? QRhiTexture::R16 : QRhiTexture::R8)
                                                            : (wide ? QRhiTexture::RG16 : QRhiTexture::RG8);
            CVMetalTextureRef tex = nullptr;
            if (CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, m_cache, pixels, nullptr, mf, size_t(pl.width),
                                                          size_t(pl.height), size_t(p), &tex) != kCVReturnSuccess)
                return false;
            held.push_back(tex);
            id<MTLTexture> mt = CVMetalTextureGetTexture(tex);
            std::unique_ptr<QRhiTexture> t(m_rhi->newTexture(qf, QSize(pl.width, pl.height)));
            if (!mt || !t->createFrom({quint64(quintptr((__bridge void *)mt)), 0})) return false;
            planes.push_back(std::move(t));
        }
        QRhiTexture *channel[3];
        for (int c = 0; c < 3; ++c) channel[c] = planes[size_t(L.srcPlane[c] >= 0 ? L.srcPlane[c] : 0)].get();
        if (!ensurePipeline(channel)) return false;

        float u[48] = {};
        for (int c = 0; c < 3; ++c)
            if (L.srcPlane[c] >= 0) u[c * 4 + std::clamp(L.srcChannel[c], 0, 3)] = 1;
        for (int c = 0; c < 3; ++c)
            if (L.mode == VideoLayout::Mode::Yuv && (c == 1 || c == 2) && L.srcPlane[c] >= 0) {
                const VideoLayout::Plane &pl = L.plane[L.srcPlane[c]];
                float *off = c == 1 ? &u[12 + 2] : &u[16];
                off[0] = L.chromaShift[0] / float(std::max(1, pl.width));
                off[1] = L.chromaShift[1] / float(std::max(1, pl.height));
            }
        std::copy(L.scale, L.scale + 4, &u[20]);
        for (int r = 0; r < 3; ++r) std::copy(L.matrix + r * 3, L.matrix + r * 3 + 3, &u[24 + r * 4]);
        u[36] = L.yOffset, u[37] = L.yScale, u[38] = L.cOffset, u[39] = L.cScale;
        u[40] = float(L.width) / float(std::max(1, L.plane[0].width));
        u[41] = float(L.height) / float(std::max(1, L.plane[0].height));
        u[42] = L.mode == VideoLayout::Mode::Yuv ? 0 : L.mode == VideoLayout::Mode::Rgb ? 1 : 2;
        u[43] = m_rhi->isYUpInNDC() ? 1 : 0; // the picture's top row at the target's top

        QRhiCommandBuffer *cb = nullptr;
        if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return false;
        QRhiResourceUpdateBatch *in = m_rhi->nextResourceUpdateBatch();
        in->updateDynamicBuffer(m_ubuf.get(), 0, sizeof u, u);
        cb->beginPass(m_rt.get(), Qt::black, {1.0f, 0}, in);
        cb->setGraphicsPipeline(m_ps.get());
        cb->setViewport({0, 0, float(L.width), float(L.height)});
        cb->setShaderResources();
        cb->draw(3);
        QRhiReadbackResult result;
        QRhiResourceUpdateBatch *outBatch = nullptr;
        if (readback) {
            outBatch = m_rhi->nextResourceUpdateBatch();
            outBatch->readBackTexture({m_out.get()}, &result);
        }
        cb->endPass(outBatch);
        const bool ok = m_rhi->endOffscreenFrame() == QRhi::FrameOpSuccess; // waits for the GPU
        m_planes = std::move(planes); // the bindings point at them until the next frame
        for (CVMetalTextureRef t : m_held) CFRelease(t);
        m_held = std::move(held);
        if (ok && readback) {
            *readback = QImage(reinterpret_cast<const uchar *>(result.data.constData()), result.pixelSize.width(),
                               result.pixelSize.height(), QImage::Format_RGBA8888)
                            .copy();
        }
        return ok;
    }

private:
    bool ensureTarget(int w, int h)
    {
        if (m_out && m_out->pixelSize() == QSize(w, h)) return true;
        m_ps.reset();
        m_srb.reset();
        m_rp.reset();
        m_rt.reset();
        m_out.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(w, h), 1,
                                      QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!m_out->create()) return false;
        m_rt.reset(m_rhi->newTextureRenderTarget({m_out.get()}));
        m_rp.reset(m_rt->newCompatibleRenderPassDescriptor());
        m_rt->setRenderPassDescriptor(m_rp.get());
        return m_rt->create();
    }
    bool ensurePipeline(QRhiTexture *const channel[3])
    {
        if (!m_srb) m_srb.reset(m_rhi->newShaderResourceBindings());
        m_srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, m_ubuf.get()),
                            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, channel[0], m_sampler.get()),
                            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, channel[1], m_sampler.get()),
                            QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage, channel[2], m_sampler.get())});
        if (!m_srb->create()) return false;
        if (m_ps) return true;
        static const QShader vs = bake(kVertex, QShader::VertexStage), fs = bake(kFragment, QShader::FragmentStage);
        if (!vs.isValid() || !fs.isValid()) return false;
        m_ps.reset(m_rhi->newGraphicsPipeline());
        m_ps->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
        m_ps->setVertexInputLayout({});
        m_ps->setShaderResourceBindings(m_srb.get());
        m_ps->setRenderPassDescriptor(m_rp.get());
        return m_ps->create();
    }

    std::unique_ptr<QRhi> m_rhi;
    id<MTLDevice> m_device = nil;
    CVMetalTextureCacheRef m_cache = nullptr;
    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_out;
    std::unique_ptr<QRhiTextureRenderTarget> m_rt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rp;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_ps;
    std::vector<std::unique_ptr<QRhiTexture>> m_planes;
    std::vector<CVMetalTextureRef> m_held;
};

struct Diff {
    int max = 0;
    double mean = 0;
    bool sameSize = false;
};
static Diff compare(const QImage &a, const QImage &b)
{
    Diff d;
    d.sameSize = a.size() == b.size();
    if (!d.sameSize) return d;
    double sum = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar *p = a.constScanLine(y), *q = b.constScanLine(y);
        for (int x = 0; x < a.width() * 4; ++x) {
            if (x % 4 == 3) continue;
            const int v = std::abs(int(p[x]) - int(q[x]));
            d.max = std::max(d.max, v);
            sum += v;
        }
    }
    d.mean = sum / (double(a.width()) * a.height() * 3);
    return d;
}

// An OpenGL texture's pixels, top row first (as the picture: VideoTexture's output is drawn bottom row first)
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

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    int frames = 60;
    if (const qsizetype i = args.indexOf(QStringLiteral("--frames")); i >= 0 && i + 1 < args.size()) {
        frames = args.takeAt(i + 1).toInt();
        args.removeAt(i);
    }
    if (args.isEmpty()) args << QStringLiteral(TEST_DIR "/media/h264.mp4");

    RhiPath rhi;
    if (!rhi.init()) {
        std::printf("SKIP: no Metal device for QRhi\n");
        return 77;
    }
    // The OpenGL path, as the engine sets it up
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
    VideoDecoder::setHardwareDecoding(true);
    VideoDecoder::setZeroCopy(conv.surfaces());

    int tested = 0;
    for (const QString &path : args) {
        VideoDecoder dec;
        if (!dec.open(path, &err)) {
            std::printf("FAIL %s: %s\n", qPrintable(path), qPrintable(err));
            ++g_failures;
            continue;
        }
        Timeline t;
        t.mode = Timeline::Loop;
        dec.setTimeline(t);
        dec.seek(0);
        VideoTexture gltex;
        VideoFrame fr;
        Stats viaRhi, viaGl;
        int got = 0;
        bool hardware = true;
        QElapsedTimer wall;
        wall.start();
        for (double clock = 0.0005; got < frames && wall.elapsed() < 60000; clock += 1.0 / std::max(1.0, dec.fps())) {
            while (!dec.fetch(clock, fr) && wall.elapsed() < 60000) QThread::usleep(200);
            if (!fr.layout) continue;
            if (!fr.layout->surface || !fr.av) {
                hardware = false;
                break;
            }
            QImage first;
            QElapsedTimer tm;
            tm.start();
            const bool ok = rhi.convert(fr, got == 0 ? &first : nullptr);
            const double rhiMs = tm.nsecsElapsed() / 1e6;
            CHECK(ok);
            if (got > 0) viaRhi.add(rhiMs); // the first frame builds the pipeline and reads back: left out
            tm.restart();
            gltex.upload(fr, dec, conv);
            f->glFinish();
            if (got > 0) viaGl.add(tm.nsecsElapsed() / 1e6);
            if (got == 0) {
                // The same frame: QRhi against the OpenGL path (the same formula, another API), and against FFmpeg
                const QImage viaOpenGl = readTexture(gltex.texture(), gltex.width(), gltex.height());
                const Diff g = compare(first, viaOpenGl);
                const QImage ref = ffmpegReference(path);
                const Diff d = compare(first, ref), o = compare(viaOpenGl, ref);
                std::printf("  %s %s: QRhi (Metal, no copy) against OpenGL: max %d mean %.3f · against FFmpeg: max %d mean %.2f "
                            "(OpenGL against FFmpeg: max %d mean %.2f)\n",
                            qPrintable(QFileInfo(path).fileName()), qPrintable(fr.layout->description), g.max, g.mean, d.max, d.mean,
                            o.max, o.mean);
                CHECK(g.sameSize && g.max <= 2 && g.mean <= 0.25); // the same picture as OpenGL's, to the rounding
                CHECK(d.sameSize && d.mean <= o.mean + 0.1);       // no further from FFmpeg than OpenGL
                if (const QByteArray dir = qgetenv("NYXMAP_RHI_DUMP"); !dir.isEmpty()) {
                    first.save(QString::fromUtf8(dir) + QStringLiteral("/rhi.png"));
                    viaOpenGl.save(QString::fromUtf8(dir) + QStringLiteral("/opengl.png"));
                    ref.save(QString::fromUtf8(dir) + QStringLiteral("/ffmpeg.png"));
                }
            }
            ++got;
        }
        gltex.destroy();
        if (!hardware) {
            std::printf("  %s: no hardware frames sampled in place (not tested)\n", qPrintable(QFileInfo(path).fileName()));
            continue;
        }
        ++tested;
        CHECK(got >= std::min(frames, 2));
        std::printf("  %s %d×%d, %d frames — per frame, GPU done: QRhi Metal median %.2f ms P95 %.2f P99 %.2f · "
                    "OpenGL median %.2f ms P95 %.2f P99 %.2f\n",
                    qPrintable(QFileInfo(path).fileName()), fr.layout ? fr.layout->width : 0, fr.layout ? fr.layout->height : 0, got,
                    viaRhi.at(0.5), viaRhi.at(0.95), viaRhi.at(0.99), viaGl.at(0.5), viaGl.at(0.95), viaGl.at(0.99));
    }
    conv.release();
    if (!tested && !g_failures) {
        std::printf("SKIP: no hardware frames here\n");
        return 77;
    }
    std::printf(g_failures ? "%d failure(s)\n" : "ok\n", g_failures);
    return g_failures ? 1 : 0;
}
