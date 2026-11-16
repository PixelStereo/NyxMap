// QRhi, a whole composition (docs/developers/qrhi-prototype.md): the same work through QRhi (Metal, Direct3D 11,
// Vulkan) and through OpenGL, to compare the APIs and nothing else.
//
// Each layer: a video (RhiVideoTexture: hardware frames on the GPU where it can), converted to RGBA, an ISF effect, then drawn into its cell of a
// grid over the output, multiplied by an RGBA mask, blended over the layers below. OpenGL runs the engine's own
// code (VideoTexture, IsfInstance::render); QRhi runs IsfInstance::rhiSources's shaders and the same formulas.
//  - pictures: the first frame of every video, through both: the outputs must match (ISF drawn on the GPU);
//  - cost: rendering as fast as it can for S seconds, each frame waited for (GPU done): frames per second and
//    frame times (median, P95, P99), for each API in turn.
//
// nyxmap_rhi_composition_test [--layers N] [--seconds S] [--size WxH] [--effect file.fs] [--opengl-first] [video]
//   (default: 4 layers of test/media/h264.mp4, 2 s, 1920×1080, Kaleidoscope). Exit 77 (skipped) without Metal,
//   OpenGL or a QRhi backend.
#include "Gl.h"
#include "Isf.h"
#include "RhiDevice.h"
#include "RhiVideoSurface.h"
#include "RhiVideoTexture.h"
#include "VideoDecoder.h"
#include "VideoTexture.h"

extern "C" {
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
}

#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QThread>
#include <rhi/qrhi.h>
#include <rhi/qshaderbaker.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

// The values the comparison sets (an effect's defaults can hide a flipped picture: a kaleidoscope is symmetric)
static void tweak(const QString &name, float *v)
{
    if (name == QLatin1String("rotation")) v[0] = 17;
    if (name == QLatin1String("center")) v[0] = 0.42f, v[1] = 0.57f;
}

// The grid cell of layer k of n, in composition coordinates (0..1, y up)
static void cell(int k, int n, float r[4])
{
    const int cols = int(std::ceil(std::sqrt(double(n)))), rows = (n + cols - 1) / cols;
    r[0] = float(k % cols) / cols;
    r[1] = float(k / cols) / rows;
    r[2] = r[0] + 1.0f / cols;
    r[3] = r[1] + 1.0f / rows;
}

// A radial RGBA mask: opaque in the middle, transparent at the edges, tinted (not symmetric in colour)
static QImage makeMask()
{
    QImage m(256, 256, QImage::Format_RGBA8888);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            const float dx = (x + 0.5f) / 128 - 1, dy = (y + 0.5f) / 128 - 1;
            const float a = std::clamp(1.4f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
            uchar *p = m.scanLine(y) + x * 4;
            p[0] = uchar(255 * a), p[1] = uchar(255 * a * (0.6f + 0.4f * x / 255.0f)), p[2] = uchar(255 * a * 0.9f), p[3] = uchar(255 * a);
        }
    return m;
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

struct Layer {
    std::unique_ptr<VideoDecoder> dec;
    VideoFrame frame;
    bool fresh = false; // a new frame to convert
};

static bool openLayers(std::vector<Layer> &layers, int n, const QString &path)
{
    layers.clear();
    layers.resize(size_t(n));
    for (Layer &l : layers) {
        l.dec = std::make_unique<VideoDecoder>();
        QString err;
        if (!l.dec->open(path, &err)) {
            std::printf("FAIL %s: %s\n", qPrintable(path), qPrintable(err));
            return false;
        }
        Timeline t;
        t.mode = Timeline::Loop;
        l.dec->setTimeline(t);
        l.dec->seek(0);
    }
    return true;
}

// Every layer's frame for the clock (waits for the first ones)
static bool fetch(std::vector<Layer> &layers, double clock, bool wait)
{
    QElapsedTimer tm;
    tm.start();
    for (Layer &l : layers) {
        bool got = l.dec->fetch(clock, l.frame);
        while (wait && !got && tm.elapsed() < 5000) {
            QThread::usleep(200);
            got = l.dec->fetch(clock, l.frame);
        }
        l.fresh = l.fresh || got;
        if (!l.frame.layout) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// QRhi (the platform's API)
// ---------------------------------------------------------------------------
static const char *kCompositeVs = R"(#version 440
layout(location = 0) out vec2 v_uv;
layout(std140, binding = 0) uniform Cell { vec4 rect; vec4 flip; };
void main() {
    const vec2 corners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
    vec2 k = corners[gl_VertexIndex];
    v_uv = k;
    vec2 p = mix(rect.xy, rect.zw, k) * 2.0 - 1.0;
    gl_Position = vec4(p.x, p.y * flip.x, 0.0, 1.0);
}
)";
static const char *kCompositeFs = R"(#version 440
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
layout(binding = 1) uniform sampler2D picture;
layout(binding = 2) uniform sampler2D mask;
void main() { o = texture(picture, v_uv) * texture(mask, v_uv); } // premultiplied, blended over
)";

static QShader bake(const QByteArray &src, QShader::Stage stage)
{
    QShaderBaker b;
    b.setSourceString(src, stage);
    b.setGeneratedShaders({{QShader::MslShader, QShaderVersion(20)},
                           {QShader::HlslShader, QShaderVersion(50)},
                           {QShader::SpirvShader, QShaderVersion(100)}});
    b.setGeneratedShaderVariants({QShader::StandardShader});
    const QShader s = b.bake();
    if (!s.isValid()) std::printf("shader: %s\n", qPrintable(b.errorMessage()));
    return s;
}

class RhiComposition
{
public:
    ~RhiComposition() { release(); }
    QRhi *rhi() const { return m_rhi.get(); }
    bool init(int layers, QSize size, const QString &effect)
    {
        m_rhi = rhidevice::create({}, &m_backend);
        if (!m_rhi) return false;
        m_flip = rhidevice::ndcFlip(m_rhi.get()); // first row: the picture's bottom, as with OpenGL
        m_size = size;
        m_isf = IsfInstance::rhiSources(effect);
        if (!m_isf.error.isEmpty() || m_isf.inputImage.isEmpty()) {
            std::printf("FAIL effect %s: %s\n", qPrintable(effect), m_isf.error.isEmpty() ? "not a filter" : qPrintable(m_isf.error));
            return false;
        }
        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                          QRhiSampler::ClampToEdge));
        m_out.reset(m_rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!m_sampler->create() || !m_out->create()) return false;
        m_outRt.reset(m_rhi->newTextureRenderTarget({m_out.get()}));
        m_outRp.reset(m_outRt->newCompatibleRenderPassDescriptor());
        m_outRt->setRenderPassDescriptor(m_outRp.get());
        if (!m_outRt->create()) return false;
        // The mask, uploaded once; an image the effect samples when it has no other: black
        const QImage mask = makeMask();
        m_mask.reset(m_rhi->newTexture(QRhiTexture::RGBA8, mask.size()));
        m_black.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        if (!m_mask->create() || !m_black->create()) return false;
        QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
        u->uploadTexture(m_mask.get(), mask);
        QImage black(1, 1, QImage::Format_RGBA8888);
        black.fill(Qt::transparent);
        u->uploadTexture(m_black.get(), black);
        m_pending = u;
        m_layers.resize(size_t(layers));
        for (size_t k = 0; k < m_layers.size(); ++k) {
            RLayer &l = m_layers[k];
            l.video = std::make_unique<RhiVideoTexture>(m_rhi.get());
            l.isfUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, std::max(16, m_isf.blockSize)));
            l.cellUbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
            if (!l.isfUbuf->create() || !l.cellUbuf->create()) return false;
            float c[8] = {0, 0, 0, 0, m_flip, 0, 0, 0};
            cell(int(k), layers, c);
            u->updateDynamicBuffer(l.cellUbuf.get(), 0, sizeof c, c);
        }
        return true;
    }

    // One frame of the composition (the layers whose frame is fresh converted again); readback: the output, first
    // row the picture's bottom (nullptr: not read). Waits for the GPU.
    bool render(std::vector<Layer> &layers, float time, QImage *readback)
    {
        QRhiCommandBuffer *cb = nullptr;
        if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return false;
        bool ok = true;
        for (size_t k = 0; k < layers.size() && ok; ++k) {
            RLayer &rl = m_layers[k];
            Layer &l = layers[k];
            if (l.fresh) { // the frame becomes the layer's picture (outside any pass)
                ok = rl.video->upload(cb, l.frame);
                l.fresh = false;
            }
            ok = ok && ensureLayer(rl) && effect(cb, rl, time);
        }
        ok = ok && composite(cb, readback);
        const bool done = m_rhi->endOffscreenFrame() == QRhi::FrameOpSuccess;
        if (done && readback && !m_readback.data.isEmpty())
            *readback = QImage(reinterpret_cast<const uchar *>(m_readback.data.constData()), m_readback.pixelSize.width(),
                               m_readback.pixelSize.height(), QImage::Format_RGBA8888)
                            .copy();
        return ok && done;
    }
    QString backend() const { return m_backend; }

private:
    struct RLayer {
        std::unique_ptr<RhiVideoTexture> video;
        QRhiTexture *input = nullptr; // the video picture the effect's bindings read
        std::unique_ptr<QRhiTexture> fx;
        std::unique_ptr<QRhiTextureRenderTarget> fxRt;
        std::unique_ptr<QRhiRenderPassDescriptor> fxRp;
        std::unique_ptr<QRhiBuffer> isfUbuf, cellUbuf;
        std::unique_ptr<QRhiShaderResourceBindings> fxSrb, compSrb;
        int frameIndex = 0;
    };

    void release()
    {
        m_layers.clear();
        m_fxPs.reset();
        m_compPs.reset();
        m_outRt.reset();
        m_outRp.reset();
        m_out.reset();
        m_mask.reset();
        m_black.reset();
        m_sampler.reset();
        m_rhi.reset();
    }

    bool ensureLayer(RLayer &l)
    {
        QRhiTexture *picture = l.video->texture();
        if (!picture) return false;
        if (l.fx && l.input == picture) return true;
        const int w = picture->pixelSize().width(), h = picture->pixelSize().height();
        l.input = picture;
        if (!l.fx || l.fx->pixelSize() != QSize(w, h)) {
            l.fx.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(w, h), 1, QRhiTexture::RenderTarget));
            if (!l.fx->create()) return false;
            l.fxRt.reset(m_rhi->newTextureRenderTarget({l.fx.get()}));
            l.fxRp.reset(l.fxRt->newCompatibleRenderPassDescriptor());
            l.fxRt->setRenderPassDescriptor(l.fxRp.get());
            if (!l.fxRt->create()) return false;
        }
        // The effect reads the converted picture; the composite reads the effect's
        l.fxSrb.reset(m_rhi->newShaderResourceBindings());
        QVector<QRhiShaderResourceBinding> fx = {QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, l.isfUbuf.get())};
        for (int s = 0; s < m_isf.samplers.size(); ++s)
            fx << QRhiShaderResourceBinding::sampledTexture(s + 1, QRhiShaderResourceBinding::FragmentStage,
                                                            m_isf.samplers[s] == m_isf.inputImage ? picture : m_black.get(),
                                                            m_sampler.get());
        l.fxSrb->setBindings(fx.cbegin(), fx.cend());
        l.compSrb.reset(m_rhi->newShaderResourceBindings());
        l.compSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, l.cellUbuf.get()),
                                QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, l.fx.get(), m_sampler.get()),
                                QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, m_mask.get(), m_sampler.get())});
        if (!l.fxSrb->create() || !l.compSrb->create()) return false;
        if (!m_fxPs) {
            m_fxPs.reset(m_rhi->newGraphicsPipeline());
            m_fxPs->setShaderStages({{QRhiShaderStage::Vertex, bake(m_isf.vertex.toUtf8(), QShader::VertexStage)},
                                     {QRhiShaderStage::Fragment, bake(m_isf.fragment.toUtf8(), QShader::FragmentStage)}});
            QRhiVertexInputLayout input; // isf_position: a quad of two triangles
            input.setBindings({{2 * sizeof(float)}});
            input.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});
            m_fxPs->setVertexInputLayout(input);
            m_fxPs->setShaderResourceBindings(l.fxSrb.get());
            m_fxPs->setRenderPassDescriptor(l.fxRp.get());
            if (!m_fxPs->create()) return false;
            m_quad.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, 12 * sizeof(float)));
            if (!m_quad->create()) return false;
            static const float quad[12] = {-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};
            if (!m_pending) m_pending = m_rhi->nextResourceUpdateBatch();
            m_pending->uploadStaticBuffer(m_quad.get(), quad);
        }
        if (!m_compPs) {
            m_compPs.reset(m_rhi->newGraphicsPipeline());
            m_compPs->setShaderStages({{QRhiShaderStage::Vertex, bake(kCompositeVs, QShader::VertexStage)},
                                       {QRhiShaderStage::Fragment, bake(kCompositeFs, QShader::FragmentStage)}});
            QRhiGraphicsPipeline::TargetBlend over;
            over.enable = true;
            over.srcColor = over.srcAlpha = QRhiGraphicsPipeline::One;
            over.dstColor = over.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            m_compPs->setTargetBlends({over});
            m_compPs->setShaderResourceBindings(l.compSrb.get());
            m_compPs->setRenderPassDescriptor(m_outRp.get());
            if (!m_compPs->create()) return false;
        }
        return true;
    }

    bool effect(QRhiCommandBuffer *cb, RLayer &rl, float time)
    {
        const QSize sz = rl.fx->pixelSize();
        std::vector<char> block(size_t(std::max(16, m_isf.blockSize)), 0);
        for (const auto &u : m_isf.uniforms) {
            float v[4];
            std::copy(u.value, u.value + 4, v);
            if (u.name == QLatin1String("RENDERSIZE")) v[0] = float(sz.width()), v[1] = float(sz.height());
            else if (u.name == QLatin1String("TIME")) v[0] = time;
            else if (u.name == QLatin1String("FRAMEINDEX")) v[0] = float(rl.frameIndex);
            else if (u.name == QLatin1String("isf_ndcFlip")) v[0] = m_flip;
            else if (u.name == QStringLiteral("_%1_imgSize").arg(m_isf.inputImage)) v[0] = float(sz.width()), v[1] = float(sz.height());
            else if (u.name == QStringLiteral("_%1_imgRect").arg(m_isf.inputImage)) v[2] = float(sz.width()), v[3] = float(sz.height());
            else tweak(u.name, v);
            char *at = block.data() + u.offset;
            switch (u.kind) {
            case IsfInstance::RhiSources::Uniform::Int:
            case IsfInstance::RhiSources::Uniform::Bool: {
                const qint32 i = qint32(std::lround(v[0]));
                std::memcpy(at, &i, 4);
                break;
            }
            case IsfInstance::RhiSources::Uniform::Float: std::memcpy(at, v, 4); break;
            case IsfInstance::RhiSources::Uniform::Vec2: std::memcpy(at, v, 8); break;
            case IsfInstance::RhiSources::Uniform::Vec4: std::memcpy(at, v, 16); break;
            }
        }
        ++rl.frameIndex;
        QRhiResourceUpdateBatch *in = takePending();
        in->updateDynamicBuffer(rl.isfUbuf.get(), 0, quint32(block.size()), block.data());
        cb->beginPass(rl.fxRt.get(), Qt::transparent, {1.0f, 0}, in);
        cb->setGraphicsPipeline(m_fxPs.get());
        cb->setViewport({0, 0, float(sz.width()), float(sz.height())});
        cb->setShaderResources(rl.fxSrb.get());
        const QRhiCommandBuffer::VertexInput vin(m_quad.get(), 0);
        cb->setVertexInput(0, 1, &vin);
        cb->draw(6);
        cb->endPass();
        return true;
    }

    bool composite(QRhiCommandBuffer *cb, QImage *readback)
    {
        cb->beginPass(m_outRt.get(), Qt::transparent, {1.0f, 0}, takePending());
        cb->setGraphicsPipeline(m_compPs.get());
        cb->setViewport({0, 0, float(m_size.width()), float(m_size.height())});
        for (RLayer &l : m_layers) {
            cb->setShaderResources(l.compSrb.get());
            cb->draw(6);
        }
        QRhiResourceUpdateBatch *out = nullptr;
        m_readback = {};
        if (readback) {
            out = m_rhi->nextResourceUpdateBatch();
            out->readBackTexture({m_out.get()}, &m_readback);
        }
        cb->endPass(out);
        return true;
    }

    QRhiResourceUpdateBatch *takePending()
    {
        QRhiResourceUpdateBatch *u = m_pending ? m_pending : m_rhi->nextResourceUpdateBatch();
        m_pending = nullptr;
        return u;
    }

    std::unique_ptr<QRhi> m_rhi;
    QString m_backend;
    float m_flip = 1;
    QSize m_size;
    IsfInstance::RhiSources m_isf;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_out, m_mask, m_black;
    std::unique_ptr<QRhiTextureRenderTarget> m_outRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_outRp;
    std::unique_ptr<QRhiGraphicsPipeline> m_fxPs, m_compPs;
    std::unique_ptr<QRhiBuffer> m_quad;
    std::vector<RLayer> m_layers;
    QRhiResourceUpdateBatch *m_pending = nullptr;
    QRhiReadbackResult m_readback;
};

// ---------------------------------------------------------------------------
// OpenGL: the engine's code
// ---------------------------------------------------------------------------
class GlComposition
{
public:
    bool init(int layers, QSize size, const QString &effect)
    {
        auto f = gl();
        const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
        f->glGenVertexArrays(1, &m_vao);
        f->glBindVertexArray(m_vao);
        f->glGenBuffers(1, &m_vbo);
        f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        f->glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        f->glBindVertexArray(0);
        f->glGenVertexArrays(1, &m_emptyVao);
        QString err;
        if (!m_conv.init(m_vao, &err)) {
            std::printf("FAIL OpenGL conversion: %s\n", qPrintable(err));
            return false;
        }
        m_size = size;
        m_out.ensure(size.width(), size.height());
        m_mask.uploadImage(makeMask());
        for (int k = 0; k < layers; ++k) {
            m_layers.push_back(std::make_unique<GLayer>());
            GLayer &l = *m_layers.back();
            l.isf = std::make_unique<IsfInstance>();
            if (!l.isf->load(effect)) {
                std::printf("FAIL effect: %s\n", qPrintable(l.isf->error()));
                return false;
            }
            for (IsfInput &in : l.isf->inputs()) {
                float v[4] = {float(in.fDefault), float(in.pDefault.x()), float(in.pDefault.y()), 0};
                if (in.type == IsfInput::Float) {
                    tweak(in.name, v);
                    in.fValue = v[0];
                } else if (in.type == IsfInput::Point2D) {
                    v[0] = float(in.pDefault.x()), v[1] = float(in.pDefault.y());
                    tweak(in.name, v);
                    in.pValue = QPointF(v[0], v[1]);
                }
            }
        }
        m_composite = compileProgram(QStringLiteral("#version 330 core\n"
                                                    "uniform vec4 rect; out vec2 v_uv;\n"
                                                    "void main() {\n"
                                                    "  const vec2 corners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));\n"
                                                    "  vec2 k = corners[gl_VertexID]; v_uv = k;\n"
                                                    "  gl_Position = vec4(mix(rect.xy, rect.zw, k) * 2.0 - 1.0, 0.0, 1.0);\n"
                                                    "}\n"),
                                     QStringLiteral("#version 330 core\n"
                                                    "in vec2 v_uv; out vec4 o; uniform sampler2D picture, mask;\n"
                                                    "void main() { o = texture(picture, v_uv) * texture(mask, v_uv); }\n"),
                                     &err);
        if (!m_composite) {
            std::printf("FAIL composite: %s\n", qPrintable(err));
            return false;
        }
        m_rc.dt = 0;
        m_rc.drawQuad = [this] {
            auto g = gl();
            g->glBindVertexArray(m_vao);
            g->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            g->glBindVertexArray(0);
        };
        m_rc.drawPoints = [](int) {};
        m_rc.blit = [](GLuint, const RenderTarget &) {};
        return true;
    }
    bool surfaces() const { return m_conv.surfaces(); }
    bool s3tc() const { return m_conv.s3tc(); }
    bool bptc() const { return m_conv.bptc(); }
    void release()
    {
        for (auto &l : m_layers) {
            l->tex.destroy();
            l->fx.destroy();
            if (l->isf) l->isf->releaseGl();
        }
        m_layers.clear();
        m_out.destroy();
        m_mask.destroy();
        m_conv.release();
        auto f = gl();
        f->glDeleteProgram(m_composite);
        f->glDeleteBuffers(1, &m_vbo);
        f->glDeleteVertexArrays(1, &m_vao);
        f->glDeleteVertexArrays(1, &m_emptyVao);
    }
    bool render(std::vector<Layer> &layers, double dt, QImage *readback)
    {
        auto f = gl();
        m_rc.dt = dt;
        for (size_t k = 0; k < layers.size(); ++k) {
            GLayer &gl_ = *m_layers[k];
            Layer &l = layers[k];
            if (l.fresh) {
                gl_.tex.upload(l.frame, *l.dec, m_conv);
                l.fresh = false;
            }
            gl_.isf->render(m_rc, gl_.tex.texture(), gl_.tex.width(), gl_.tex.height(), gl_.fx, gl_.tex.width(), gl_.tex.height());
        }
        m_out.bind();
        f->glClearColor(0, 0, 0, 0);
        f->glClear(GL_COLOR_BUFFER_BIT);
        f->glEnable(GL_BLEND);
        f->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        f->glUseProgram(m_composite);
        f->glUniform1i(f->glGetUniformLocation(m_composite, "picture"), 0);
        f->glUniform1i(f->glGetUniformLocation(m_composite, "mask"), 1);
        f->glActiveTexture(GL_TEXTURE1);
        f->glBindTexture(GL_TEXTURE_2D, m_mask.tex);
        f->glBindVertexArray(m_emptyVao);
        for (size_t k = 0; k < m_layers.size(); ++k) {
            float r[4];
            cell(int(k), int(m_layers.size()), r);
            f->glUniform4fv(f->glGetUniformLocation(m_composite, "rect"), 1, r);
            f->glActiveTexture(GL_TEXTURE0);
            f->glBindTexture(GL_TEXTURE_2D, m_layers[k]->fx.tex);
            f->glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        f->glBindVertexArray(0);
        f->glDisable(GL_BLEND);
        if (readback) {
            QImage img(m_size, QImage::Format_RGBA8888);
            f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            f->glReadPixels(0, 0, m_size.width(), m_size.height(), GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
            *readback = img; // first row: the bottom
        }
        f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        f->glFinish();
        return true;
    }

private:
    struct GLayer {
        VideoTexture tex;
        std::unique_ptr<IsfInstance> isf;
        RenderTarget fx;
    };
    VideoConverter m_conv;
    GLuint m_vao = 0, m_vbo = 0, m_emptyVao = 0, m_composite = 0;
    QSize m_size;
    RenderTarget m_out;
    Texture2D m_mask;
    std::vector<std::unique_ptr<GLayer>> m_layers;
    IsfRenderContext m_rc;
};

// ---------------------------------------------------------------------------
struct Diff {
    int max = 0;
    double mean = 0, over8 = 0; // share of channels more than 8 apart
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
            d.over8 += v > 8;
        }
    }
    d.mean = sum / (double(a.width()) * a.height() * 4);
    d.over8 /= double(a.width()) * a.height() * 4;
    return d;
}

// Renders as fast as it can for `seconds`, the videos playing in real time
template <class Draw>
static Stats run(std::vector<Layer> &layers, double seconds, Draw draw, int *converted)
{
    Stats st;
    *converted = 0;
    QElapsedTimer wall;
    wall.start();
    double last = 0;
    while (wall.elapsed() < seconds * 1000) {
        const double clock = wall.nsecsElapsed() / 1e9;
        for (Layer &l : layers)
            if (l.dec->fetch(clock, l.frame)) {
                l.fresh = true;
                ++*converted;
            }
        QElapsedTimer tm;
        tm.start();
        if (!draw(clock - last)) {
            ++g_failures;
            break;
        }
        st.ms.push_back(tm.nsecsElapsed() / 1e6);
        last = clock;
    }
    return st;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    int n = 4;
    double seconds = 2;
    QSize size(1920, 1080);
    QString effect = QStringLiteral(SOURCE_DIR "/isf/effects/Kaleidoscope.fs");
    auto option = [&](const char *name) -> QString {
        const qsizetype i = args.indexOf(QLatin1String(name));
        if (i < 0 || i + 1 >= args.size()) return {};
        const QString v = args.takeAt(i + 1);
        args.removeAt(i);
        return v;
    };
    if (const QString v = option("--layers"); !v.isEmpty()) n = std::max(1, v.toInt());
    if (const QString v = option("--seconds"); !v.isEmpty()) seconds = v.toDouble();
    if (const QString v = option("--effect"); !v.isEmpty()) effect = v;
    if (const QString v = option("--size"); !v.isEmpty()) size = QSize(v.section('x', 0, 0).toInt(), v.section('x', 1, 1).toInt());
    const bool openGlFirstArg = args.contains(QStringLiteral("--opengl-first"));
    args.removeAll(QStringLiteral("--opengl-first"));
    const QString video = args.isEmpty() ? QStringLiteral(SOURCE_DIR "/test/media/h264.mp4") : args.first();

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
    VideoDecoder::setHardwareDecoding(true);
    GlComposition glc;
    RhiComposition rhi;
    if (!glc.init(n, size, effect)) return 1;
    if (!rhi.init(n, size, effect)) {
        std::printf("SKIP: no QRhi backend here\n");
        return 77;
    }
    std::printf("%d layers of %s, %s, output %d×%d, QRhi %s\n", n, qPrintable(QFileInfo(video).fileName()),
                qPrintable(QFileInfo(effect).completeBaseName()), size.width(), size.height(), qPrintable(rhi.backend()));
    // Each API's decoding: QRhi's on its device where it shares one (Windows), hardware frames taken on the GPU;
    // OpenGL's as the OpenGL renderer sets it
    auto forRhi = [&] {
        AVBufferRef *device = RhiVideoSurface::decoderDevice(rhi.rhi());
        VideoDecoder::setSharedHardwareDevice(device);
        av_buffer_unref(&device);
        VideoDecoder::setZeroCopy(RhiVideoSurface::available(rhi.rhi()));
        VideoDecoder::setGpuFormats(rhi.rhi()->isTextureFormatSupported(QRhiTexture::BC1),
                                    rhi.rhi()->isTextureFormatSupported(QRhiTexture::BC7));
    };
    auto forOpenGl = [&] {
        VideoDecoder::setSharedHardwareDevice(nullptr);
        VideoDecoder::setZeroCopy(glc.surfaces());
        VideoDecoder::setGpuFormats(glc.s3tc(), glc.bptc());
    };

    // The same first frames through both, each from fresh decoders: the pictures must match
    std::vector<Layer> layers;
    QImage viaRhi, viaGl;
    forRhi();
    if (!openLayers(layers, n, video) || !fetch(layers, 0.0005, true)) return 1;
    CHECK(rhi.render(layers, 0, &viaRhi));
    forOpenGl();
    if (!openLayers(layers, n, video) || !fetch(layers, 0.0005, true)) return 1;
    CHECK(glc.render(layers, 0, &viaGl));
    const Diff d = compare(viaRhi, viaGl);
    std::printf("  first frame, QRhi against OpenGL: max %d mean %.3f, %.2f%% over 8\n", d.max, d.mean, d.over8 * 100);
    // The same picture: rounding everywhere, and on some GPUs (paravirtualized ones) a few edge pixels filtered apart
    CHECK(d.sameSize && d.mean <= 0.5 && d.over8 <= 0.005);
    if (const QByteArray dir = qgetenv("NYXMAP_RHI_DUMP"); !dir.isEmpty()) {
        viaRhi.mirrored(false, true).save(QString::fromUtf8(dir) + QStringLiteral("/composition_rhi.png"));
        viaGl.mirrored(false, true).save(QString::fromUtf8(dir) + QStringLiteral("/composition_opengl.png"));
    }

    // The cost: each API in turn, fresh decoders, the same work
    const bool openGlFirst = openGlFirstArg; // the order's influence
    for (int turn = 0; turn < 2; ++turn) {
        const int api = openGlFirst ? 1 - turn : turn;
        if (api == 0) forRhi();
        else forOpenGl();
        if (!openLayers(layers, n, video) || !fetch(layers, 0.0005, true)) return 1;
        int converted = 0;
        const Stats st = api == 0 ? run(layers, seconds, [&](double) { return rhi.render(layers, 0, nullptr); }, &converted)
                                  : run(layers, seconds, [&](double dt) { return glc.render(layers, dt, nullptr); }, &converted);
        const QString label = api == 0 ? QStringLiteral("QRhi ") + rhi.backend() : QStringLiteral("OpenGL");
        std::printf("  %-14s %6.1f frames/s; frame (GPU done) median %.2f ms, P95 %.2f, P99 %.2f; %.1f video frames/s per layer\n",
                    qPrintable(label), st.ms.size() / seconds, st.at(0.5), st.at(0.95), st.at(0.99), converted / seconds / n);
    }
    VideoDecoder::setSharedHardwareDevice(nullptr);
    layers.clear();
    glc.release();
    std::printf(g_failures ? "%d failure(s)\n" : "ok\n", g_failures);
    return g_failures ? 1 : 0;
}
