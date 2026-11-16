#include "RhiResources.h"
#include "RhiDevice.h"
#include "RhiTarget.h"

#include <QFile>
#include <QImage>
#include <map>
#include <mutex>

static std::mutex s_mutex;
static std::map<QRhi *, std::unique_ptr<RhiResources>> s_sets;

RhiResources &RhiResources::of(QRhi *rhi)
{
    std::lock_guard<std::mutex> lk(s_mutex);
    auto &set = s_sets[rhi];
    if (!set) set.reset(new RhiResources(rhi));
    return *set;
}

void RhiResources::release(QRhi *rhi)
{
    std::lock_guard<std::mutex> lk(s_mutex);
    s_sets.erase(rhi);
}

QShader RhiResources::shader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}

RhiResources::RhiResources(QRhi *rhi) : m_rhi(rhi), m_flip(rhidevice::ndcFlip(rhi))
{
    m_linear.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                   QRhiSampler::ClampToEdge));
    m_nearest.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                    QRhiSampler::ClampToEdge));
    m_linear->create();
    m_nearest->create();
    m_black.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_black->create();
    m_quad.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, 12 * sizeof(float)));
    m_quad->create();
    m_blitUbuf.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::UniformBuffer, 16));
    m_blitUbuf->create();
    m_uploads = rhi->nextResourceUpdateBatch();
    QImage black(1, 1, QImage::Format_RGBA8888);
    black.fill(Qt::transparent);
    m_uploads->uploadTexture(m_black.get(), black);
    static const float quad[12] = {-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};
    m_uploads->uploadStaticBuffer(m_quad.get(), quad);
    const float flip[4] = {m_flip, 0, 0, 0};
    m_uploads->uploadStaticBuffer(m_blitUbuf.get(), flip);
}

RhiResources::~RhiResources()
{
    if (m_uploads) m_uploads->release();
}

QRhiResourceUpdateBatch *RhiResources::takeUploads()
{
    QRhiResourceUpdateBatch *u = m_uploads;
    m_uploads = nullptr;
    return u;
}

void RhiResources::frameStarted()
{
    ++m_frame;
    // Kept a few frames after their last use: QRhi has at most three frames in flight
    if (m_bindings.size() > 64)
        for (auto it = m_bindings.begin(); it != m_bindings.end();)
            it = m_frame - it->second.used > 8 ? m_bindings.erase(it) : std::next(it);
}

QRhiShaderResourceBindings *RhiResources::bindings(const std::vector<Bind> &list)
{
    std::vector<quint64> key;
    key.reserve(list.size() * 4);
    for (const Bind &b : list) {
        key.push_back(quint64(b.binding));
        key.push_back(quint64(b.stages.toInt()));
        key.push_back(b.ubuf ? b.ubuf->globalResourceId() : 0);
        key.push_back(b.tex ? b.tex->globalResourceId() : 0);
        key.push_back(b.sampler ? b.sampler->globalResourceId() : 0);
    }
    Kept &k = m_bindings[key];
    k.used = m_frame;
    if (!k.srb) {
        std::vector<QRhiShaderResourceBinding> rb;
        for (const Bind &b : list)
            rb.push_back(b.ubuf ? QRhiShaderResourceBinding::uniformBuffer(b.binding, b.stages, b.ubuf)
                                : QRhiShaderResourceBinding::sampledTexture(b.binding, b.stages, b.tex, b.sampler));
        k.srb.reset(m_rhi->newShaderResourceBindings());
        k.srb->setBindings(rb.cbegin(), rb.cend());
        if (!k.srb->create()) {
            m_bindings.erase(key);
            return nullptr;
        }
    }
    return k.srb.get();
}

bool RhiResources::blit(QRhiCommandBuffer *cb, QRhiTexture *src, RhiTarget &dst, QRhiResourceUpdateBatch *uploads)
{
    if (!src || !dst.ready()) return false;
    QRhiShaderResourceBindings *srb = bindings({{0, QRhiShaderResourceBinding::VertexStage, m_blitUbuf.get()},
                                                {1, QRhiShaderResourceBinding::FragmentStage, nullptr, src, m_linear.get()}});
    const int slot = dst.format() == QRhiTexture::RGBA32F ? 2 : dst.format() == QRhiTexture::RGBA16F ? 1 : 0;
    std::unique_ptr<QRhiGraphicsPipeline> &ps = m_blitPs[slot];
    if (!ps && srb) {
        ps.reset(m_rhi->newGraphicsPipeline());
        ps->setShaderStages({{QRhiShaderStage::Vertex, shader(QStringLiteral("blit.vert"))},
                             {QRhiShaderStage::Fragment, shader(QStringLiteral("blit.frag"))}});
        ps->setShaderResourceBindings(srb);
        ps->setRenderPassDescriptor(dst.passDescriptor());
        if (!ps->create()) ps.reset();
    }
    QRhiResourceUpdateBatch *first = takeUploads();
    if (first && uploads) first->merge(uploads), uploads->release();
    cb->beginPass(dst.cleared(), Qt::transparent, {1.0f, 0}, first ? first : uploads);
    if (ps && srb) {
        cb->setGraphicsPipeline(ps.get());
        cb->setViewport({0, 0, float(dst.width()), float(dst.height())});
        cb->setShaderResources(srb);
        cb->draw(3);
    }
    cb->endPass();
    return ps && srb;
}

void RhiResources::clear(QRhiCommandBuffer *cb, RhiTarget &dst, QRhiResourceUpdateBatch *uploads)
{
    if (!dst.ready()) return;
    QRhiResourceUpdateBatch *first = takeUploads();
    if (first && uploads) first->merge(uploads), uploads->release();
    cb->beginPass(dst.cleared(), Qt::transparent, {1.0f, 0}, first ? first : uploads);
    cb->endPass();
}
