#include "RhiDraw.h"

#include <QFile>
#include <QMatrix4x4>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cstring>

static QShader loadShader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}

RhiDraw::RhiDraw() = default;
RhiDraw::~RhiDraw() { release(); }

void RhiDraw::release()
{
    m_picture.reset();
    m_pictureOver.reset();
    m_lines.reset();
    m_triangles.reset();
    m_pictureBindings.reset();
    m_colorBindings.reset();
    m_sampler.reset();
    m_texture.reset();
    m_vertices.reset();
    m_uniforms.reset();
    m_vertexCapacity = 0;
    m_pictureSize = QSize();
    m_pending = QImage();
    m_passKey = nullptr;
    m_rhi = nullptr;
    m_target = nullptr;
}

void RhiDraw::initialize(QRhi *rhi, QRhiRenderTarget *target)
{
    if (m_rhi && (m_rhi != rhi || m_passKey != target->renderPassDescriptor())) release();
    if (m_rhi) {
        m_target = target;
        return;
    }
    m_rhi = rhi;
    m_target = target;
    m_passKey = target->renderPassDescriptor();
    m_uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    m_uniforms->create();
    m_sampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                    QRhiSampler::ClampToEdge));
    m_sampler->create();
    // The bindings of the color pipelines have the uniform block alone; the picture's, the picture too (made at its first picture)
    m_colorBindings.reset(rhi->newShaderResourceBindings());
    m_colorBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_uniforms.get())});
    m_colorBindings->create();

    const QShader vs = loadShader(QStringLiteral("uidraw.vert")), fsTex = loadShader(QStringLiteral("uidraw_tex.frag")),
                  fsColor = loadShader(QStringLiteral("uidraw_color.frag"));
    QRhiVertexInputLayout layout;
    layout.setBindings({QRhiVertexInputBinding(8 * sizeof(float))});
    layout.setAttributes({QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
                          QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)),
                          QRhiVertexInputAttribute(0, 2, QRhiVertexInputAttribute::Float4, 4 * sizeof(float))});
    // The picture's bindings need a texture: a 1 × 1 one until the first picture
    m_texture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_texture->create();
    m_pictureBindings.reset(rhi->newShaderResourceBindings());
    m_pictureBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_uniforms.get()),
                                    QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                              m_texture.get(), m_sampler.get())});
    m_pictureBindings->create();
    auto make = [&](const QShader &fs, QRhiGraphicsPipeline::Topology topology, bool blendAlpha, bool blendPremultiplied,
                    QRhiShaderResourceBindings *bindings) {
        std::unique_ptr<QRhiGraphicsPipeline> p(rhi->newGraphicsPipeline());
        p->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
        p->setVertexInputLayout(layout);
        p->setShaderResourceBindings(bindings);
        p->setRenderPassDescriptor(target->renderPassDescriptor());
        p->setSampleCount(target->sampleCount());
        p->setTopology(topology);
        p->setCullMode(QRhiGraphicsPipeline::None);
        if (blendAlpha || blendPremultiplied) {
            QRhiGraphicsPipeline::TargetBlend b;
            b.enable = true;
            b.srcColor = blendPremultiplied ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::SrcAlpha;
            b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            b.srcAlpha = QRhiGraphicsPipeline::One;
            b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            p->setTargetBlends({b});
        }
        p->create();
        return p;
    };
    m_picture = make(fsTex, QRhiGraphicsPipeline::TriangleStrip, false, false, m_pictureBindings.get());
    m_pictureOver = make(fsTex, QRhiGraphicsPipeline::TriangleStrip, false, true, m_pictureBindings.get());
    m_lines = make(fsColor, QRhiGraphicsPipeline::Lines, true, false, m_colorBindings.get());
    m_triangles = make(fsColor, QRhiGraphicsPipeline::Triangles, true, false, m_colorBindings.get());
}

void RhiDraw::setPicture(const QImage &image) // RGBA8888, the bottom row first
{
    if (!m_rhi || image.isNull()) return;
    m_pending = image.format() == QImage::Format_RGBA8888 ? image : image.convertToFormat(QImage::Format_RGBA8888);
    if (m_pending.size() != m_pictureSize) { // another size: a texture of its own, the bindings made again
        m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, m_pending.size()));
        m_texture->create();
        m_pictureSize = m_pending.size();
        m_pictureBindings->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_uniforms.get()),
             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_texture.get(), m_sampler.get())});
        m_pictureBindings->create();
    }
}

void RhiDraw::begin(const QColor &background)
{
    m_background = background;
    m_data.clear();
    m_commands.clear();
}

void RhiDraw::add(Kind kind, const std::vector<float> &ndc, const QColor &c)
{
    const int count = int(ndc.size() / 2);
    if (count < (kind == Kind::Lines ? 2 : 3)) return;
    const int first = int(m_data.size() / 8);
    for (int k = 0; k < count; ++k) {
        m_data.insert(m_data.end(), {ndc[size_t(k) * 2], ndc[size_t(k) * 2 + 1], 0.f, 0.f, float(c.redF()), float(c.greenF()),
                                     float(c.blueF()), float(c.alphaF())});
    }
    m_commands.push_back({kind, first, count});
}

void RhiDraw::drawLines(const std::vector<float> &ndc, const QColor &c) { add(Kind::Lines, ndc, c); }
void RhiDraw::drawTriangles(const std::vector<float> &ndc, const QColor &c) { add(Kind::Triangles, ndc, c); }

void RhiDraw::drawPicture(float left, float bottom, float right, float top, bool over)
{
    if (!hasPicture()) return;
    const int first = int(m_data.size() / 8);
    const float a = over ? 0.f : 1.f; // alpha channel of the color: 1 forces an opaque picture
    // A strip: bottom left, bottom right, top left, top right; the picture's first row is its bottom (as the engine gives it)
    m_data.insert(m_data.end(), {left, bottom, 0.f, 0.f, 1.f, 1.f, 1.f, a, right, bottom, 1.f, 0.f, 1.f, 1.f, 1.f, a,
                                 left, top, 0.f, 1.f, 1.f, 1.f, 1.f, a, right, top, 1.f, 1.f, 1.f, 1.f, 1.f, a});
    m_commands.push_back({over ? Kind::PictureOver : Kind::Picture, first, 4});
}

void RhiDraw::render(QRhiCommandBuffer *cb, QRhiRenderTarget *target)
{
    if (!m_rhi) return;
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    const qsizetype bytes = qsizetype(m_data.size() * sizeof(float));
    if (bytes > m_vertexCapacity || !m_vertices) {
        m_vertexCapacity = std::max<qsizetype>(bytes, 4096) * 2;
        m_vertices.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, quint32(m_vertexCapacity)));
        m_vertices->create();
    }
    if (bytes > 0) u->updateDynamicBuffer(m_vertices.get(), 0, quint32(bytes), m_data.data());
    const QMatrix4x4 clip = m_rhi->clipSpaceCorrMatrix();
    u->updateDynamicBuffer(m_uniforms.get(), 0, 64, clip.constData());
    if (!m_pending.isNull()) {
        u->uploadTexture(m_texture.get(), m_pending);
        m_pending = QImage();
    }
    cb->beginPass(target, m_background, {1.0f, 0}, u);
    const QSize pixels = target->pixelSize();
    cb->setViewport(QRhiViewport(0, 0, float(pixels.width()), float(pixels.height())));
    for (const Command &c : m_commands) {
        QRhiGraphicsPipeline *p = c.kind == Kind::Picture       ? m_picture.get()
                                  : c.kind == Kind::PictureOver ? m_pictureOver.get()
                                  : c.kind == Kind::Lines       ? m_lines.get()
                                                                : m_triangles.get();
        cb->setGraphicsPipeline(p);
        cb->setShaderResources();
        const QRhiCommandBuffer::VertexInput input(m_vertices.get(), quint32(c.first) * 8 * sizeof(float));
        cb->setVertexInput(0, 1, &input);
        cb->draw(quint32(c.count));
    }
    cb->endPass();
}
