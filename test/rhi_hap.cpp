// QRhi prototype, HAP (docs/developers/qrhi-prototype.md): HAP's compressed blocks uploaded as they are into QRhi's
// block-compressed textures (BC1, BC3, BC4, BC7: decompressed by the GPU as it samples), then one draw makes RGBA
// with the OpenGL path's formula (RGB(A), scaled YCoCg, alpha). The same frames go through the OpenGL path
// (VideoTexture::upload, from the decoder's memory as here) to compare.
//  - colours: the first frame, QRhi against OpenGL;
//  - cost: upload and conversion of each frame, GPU done, median, P95, P99.
//
// nyxmap_rhi_hap_test [--frames N] [HAP files...]   (default: test/media hap1.mov, hap5.mov, hapy.mov)
// Exit 77 (skipped) without Metal, OpenGL, or any of the files.
#include "Gl.h"
#include "VideoDecoder.h"
#include "VideoTexture.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QThread>
#include <rhi/qrhi.h>
#include <rhi/qshaderbaker.h>

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
layout(std140, binding = 0) uniform Conversion { vec4 crop; vec4 mode; };
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    v_uv = p * 0.5 + 0.5;
    gl_Position = vec4(p.x, p.y * mode.z, 0.0, 1.0);
}
)";
// VideoTexture's HAP modes: 3 RGB(A), 4 scaled YCoCg, 5 scaled YCoCg + alpha (second texture), 6 alpha only
static const char *kFragment = R"(#version 440
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(std140, binding = 0) uniform Conversion {
    vec4 crop; // xy: part of the textures holding the picture, zw: never beyond its last texel centre
    vec4 mode; // x: mode, y: alpha used, z: NDC flip
};
layout(binding = 1) uniform sampler2D t0;
layout(binding = 2) uniform sampler2D t1;
void main() {
    vec2 uv = min(vec2(v_uv.x, 1.0 - v_uv.y) * crop.xy, crop.zw); // stored top row first
    vec4 c = texture(t0, uv);
    if (mode.x < 3.5) { o = vec4(clamp(c.rgb, 0.0, 1.0), mode.y > 0.5 ? c.a : 1.0); return; }
    if (mode.x > 5.5) { o = vec4(c.rrr, 1.0); return; }
    float s = c.z * (255.0 / 8.0) + 1.0;
    float co = (c.x - 128.0 / 255.0) / s, cg = (c.y - 128.0 / 255.0) / s;
    vec3 rgb = vec3(c.w + co - cg, c.w + cg, c.w - co - cg);
    o = vec4(clamp(rgb, 0.0, 1.0), mode.x > 4.5 ? texture(t1, uv).r : 1.0);
}
)";

static QShader bake(const char *src, QShader::Stage stage)
{
    QShaderBaker b;
    b.setSourceString(QByteArray(src), stage);
    b.setGeneratedShaders({{QShader::MslShader, QShaderVersion(20)},
                           {QShader::HlslShader, QShaderVersion(50)},
                           {QShader::SpirvShader, QShaderVersion(100)}});
    b.setGeneratedShaderVariants({QShader::StandardShader});
    const QShader s = b.bake();
    if (!s.isValid()) std::printf("shader: %s\n", qPrintable(b.errorMessage()));
    return s;
}

static bool blockFormat(VideoLayout::Texels t, QRhiTexture::Format *f)
{
    switch (t) {
    case VideoLayout::Texels::Dxt1: *f = QRhiTexture::BC1; return true;
    case VideoLayout::Texels::Dxt5: *f = QRhiTexture::BC3; return true;
    case VideoLayout::Texels::Rgtc1: *f = QRhiTexture::BC4; return true;
    case VideoLayout::Texels::Bc7: *f = QRhiTexture::BC7; return true;
    default: return false; // BC6 (HDR) and planes decoded on the CPU: not in this probe
    }
}

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

class RhiHap
{
public:
    bool init()
    {
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
        QRhiMetalInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Metal, &params));
#elif defined(Q_OS_WIN)
        QRhiD3D11InitParams params;
        m_rhi.reset(QRhi::create(QRhi::D3D11, &params));
#endif
        if (!m_rhi) return false;
        m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        return m_ubuf->create() && m_sampler->create();
    }
    QRhi *rhi() const { return m_rhi.get(); }
    ~RhiHap()
    {
        m_ps.reset();
        m_srb.reset();
        m_rp.reset();
        m_rt.reset();
        m_out.reset();
        m_planes[0].reset();
        m_planes[1].reset();
        m_sampler.reset();
        m_ubuf.reset();
        m_rhi.reset();
    }

    // Uploads the frame's blocks and converts them; readback: the picture, top row first
    bool convert(const VideoFrame &f, QImage *readback)
    {
        const VideoLayout &L = *f.layout;
        const uint8_t *data = f.bytes.data();
        if (f.staging >= 0 || f.bytes.size() < L.totalBytes || L.planeCount > 2) return false;
        if (!ensure(L)) return false;
        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        for (int p = 0; p < L.planeCount; ++p) {
            const VideoLayout::Plane &pl = L.plane[p];
            QRhiTextureSubresourceUploadDescription blocks(data + pl.offset, quint32(pl.size));
            u->uploadTexture(m_planes[p].get(), QRhiTextureUploadDescription({0, 0, blocks}));
        }
        const int mode = L.mode == VideoLayout::Mode::HapRgb ? 3 : L.mode == VideoLayout::Mode::HapYCoCg ? 4
                         : L.mode == VideoLayout::Mode::HapYCoCgAlpha ? 5 : 6;
        const VideoLayout::Plane &p0 = L.plane[0];
        const float c[8] = {float(L.width) / float(p0.width), float(L.height) / float(p0.height),
                            (float(L.width) - 0.5f) / float(p0.width), (float(L.height) - 0.5f) / float(p0.height),
                            float(mode), L.alpha ? 1.0f : 0.0f, m_rhi->isYUpInNDC() ? 1.0f : -1.0f, 0};
        u->updateDynamicBuffer(m_ubuf.get(), 0, sizeof c, c);
        QRhiCommandBuffer *cb = nullptr;
        if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return false;
        cb->beginPass(m_rt.get(), Qt::black, {1.0f, 0}, u);
        cb->setGraphicsPipeline(m_ps.get());
        cb->setViewport({0, 0, float(L.width), float(L.height)});
        cb->setShaderResources();
        cb->draw(3);
        QRhiReadbackResult result;
        QRhiResourceUpdateBatch *out = nullptr;
        if (readback) {
            out = m_rhi->nextResourceUpdateBatch();
            out->readBackTexture({m_out.get()}, &result);
        }
        cb->endPass(out);
        const bool ok = m_rhi->endOffscreenFrame() == QRhi::FrameOpSuccess;
        if (ok && readback) {
            QImage img(reinterpret_cast<const uchar *>(result.data.constData()), result.pixelSize.width(), result.pixelSize.height(),
                       QImage::Format_RGBA8888);
            *readback = m_rhi->isYUpInFramebuffer() ? img.mirrored(false, true) : img.copy();
        }
        return ok;
    }

private:
    bool ensure(const VideoLayout &L)
    {
        bool remade = false;
        for (int p = 0; p < L.planeCount; ++p) {
            QRhiTexture::Format f;
            if (!blockFormat(L.plane[p].texels, &f) || !m_rhi->isTextureFormatSupported(f)) return false;
            const QSize sz(L.plane[p].width, L.plane[p].height);
            if (m_planes[p] && m_planes[p]->format() == f && m_planes[p]->pixelSize() == sz) continue;
            m_planes[p].reset(m_rhi->newTexture(f, sz));
            if (!m_planes[p]->create()) return false;
            remade = true;
        }
        if (!m_out || m_out->pixelSize() != QSize(L.width, L.height)) {
            m_ps.reset();
            m_rt.reset();
            m_rp.reset();
            m_out.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(L.width, L.height), 1,
                                          QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
            if (!m_out->create()) return false;
            m_rt.reset(m_rhi->newTextureRenderTarget({m_out.get()}));
            m_rp.reset(m_rt->newCompatibleRenderPassDescriptor());
            m_rt->setRenderPassDescriptor(m_rp.get());
            if (!m_rt->create()) return false;
            remade = true;
        }
        if (!remade && m_ps) return true;
        QRhiTexture *second = m_planes[L.planeCount > 1 ? 1 : 0].get();
        m_srb.reset(m_rhi->newShaderResourceBindings());
        const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        m_srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, m_ubuf.get()),
                            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_planes[0].get(), m_sampler.get()),
                            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, second, m_sampler.get())});
        if (!m_srb->create()) return false;
        static const QShader vs = bake(kVertex, QShader::VertexStage), fs = bake(kFragment, QShader::FragmentStage);
        m_ps.reset(m_rhi->newGraphicsPipeline());
        m_ps->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
        m_ps->setShaderResourceBindings(m_srb.get());
        m_ps->setRenderPassDescriptor(m_rp.get());
        return m_ps->create();
    }

    std::unique_ptr<QRhi> m_rhi;
    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_planes[2], m_out;
    std::unique_ptr<QRhiTextureRenderTarget> m_rt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rp;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_ps;
};

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
        frames = args.takeAt(i + 1).toInt();
        args.removeAt(i);
    }
    if (args.isEmpty())
        for (const char *f : {"hap1.mov", "hap5.mov", "hapy.mov"})
            if (QFile::exists(QStringLiteral(TEST_DIR "/media/") + QLatin1String(f))) args << QStringLiteral(TEST_DIR "/media/") + QLatin1String(f);
    if (args.isEmpty()) {
        std::printf("SKIP: no HAP media (test/make_media.sh needs an FFmpeg with the HAP encoder)\n");
        return 77;
    }
    RhiHap rhi;
    if (!rhi.init()) {
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
    VideoDecoder::setGpuFormats(conv.s3tc(), conv.bptc());
    std::printf("BC1/BC3/BC4/BC7 in QRhi: %d %d %d %d\n", rhi.rhi()->isTextureFormatSupported(QRhiTexture::BC1),
                rhi.rhi()->isTextureFormatSupported(QRhiTexture::BC3), rhi.rhi()->isTextureFormatSupported(QRhiTexture::BC4),
                rhi.rhi()->isTextureFormatSupported(QRhiTexture::BC7));

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
        QElapsedTimer wall;
        wall.start();
        for (double clock = 0.0005; got < frames && wall.elapsed() < 60000; clock += 1.0 / std::max(1.0, dec.fps())) {
            while (!dec.fetch(clock, fr) && wall.elapsed() < 60000) QThread::usleep(200);
            if (!fr.layout) continue;
            QImage first;
            QElapsedTimer tm;
            tm.start();
            const bool ok = rhi.convert(fr, got == 0 ? &first : nullptr);
            const double rhiMs = tm.nsecsElapsed() / 1e6;
            if (!ok) {
                std::printf("  %s %s: not converted by this probe\n", qPrintable(QFileInfo(path).fileName()),
                            qPrintable(fr.layout->description));
                ++g_failures;
                break;
            }
            if (got > 0) viaRhi.ms.push_back(rhiMs); // the first frame builds the pipeline and reads back
            tm.restart();
            gltex.upload(fr, dec, conv);
            f->glFinish();
            if (got > 0) viaGl.ms.push_back(tm.nsecsElapsed() / 1e6);
            if (got == 0) {
                const Diff d = compare(first, readTexture(gltex.texture(), gltex.width(), gltex.height()));
                std::printf("  %s %s: QRhi against OpenGL: max %d mean %.3f\n", qPrintable(QFileInfo(path).fileName()),
                            qPrintable(fr.layout->description), d.max, d.mean);
                CHECK(d.sameSize && d.max <= 2 && d.mean <= 0.25);
            }
            ++got;
        }
        gltex.destroy();
        if (got > 1)
            std::printf("  %s %d×%d, %d frames — upload and conversion, GPU done: QRhi median %.2f ms P95 %.2f P99 %.2f · "
                        "OpenGL median %.2f ms P95 %.2f P99 %.2f\n",
                        qPrintable(QFileInfo(path).fileName()), fr.layout->width, fr.layout->height, got, viaRhi.at(0.5),
                        viaRhi.at(0.95), viaRhi.at(0.99), viaGl.at(0.5), viaGl.at(0.95), viaGl.at(0.99));
    }
    conv.release();
    std::printf(g_failures ? "%d failure(s)\n" : "ok\n", g_failures);
    return g_failures ? 1 : 0;
}
