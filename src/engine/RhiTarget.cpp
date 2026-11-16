#include "RhiTarget.h"
#include "VideoBudget.h"

RhiTarget::RhiTarget(RhiTarget &&o) noexcept { *this = std::move(o); }

RhiTarget &RhiTarget::operator=(RhiTarget &&o) noexcept
{
    if (this != &o) {
        release();
        m_rhi = o.m_rhi;
        m_size = o.m_size;
        m_format = o.m_format;
        m_mipped = o.m_mipped;
        m_samples = o.m_samples;
        m_msRb = std::move(o.m_msRb);
        m_msRt = std::move(o.m_msRt);
        m_msRp = std::move(o.m_msRp);
        m_tex = std::move(o.m_tex);
        m_rt = std::move(o.m_rt);
        m_keep = std::move(o.m_keep);
        m_rp = std::move(o.m_rp);
        m_keepRp = std::move(o.m_keepRp);
        o.m_size = QSize(); // its count went with it
    }
    return *this;
}

size_t RhiTarget::bytes() const
{
    if (!m_tex) return 0;
    const size_t texel = m_format == QRhiTexture::RGBA32F ? 16 : m_format == QRhiTexture::RGBA16F ? 8 : 4;
    return size_t(m_size.width()) * size_t(m_size.height()) * texel;
}

void RhiTarget::release()
{
    videoBudget::removeGpu(videoBudget::Gpu::Targets, bytes());
    m_msRt.reset();
    m_msRp.reset();
    m_msRb.reset();
    m_samples = 1;
    m_mipped = false;
    m_keep.reset();
    m_keepRp.reset();
    m_rt.reset();
    m_rp.reset();
    m_tex.reset(); // QRhi releases the native texture once no frame in flight uses it
    m_size = QSize();
}

bool RhiTarget::ensure(QRhi *rhi, QSize size, QRhiTexture::Format format) { return make(rhi, size, format, false); }
bool RhiTarget::ensureMipped(QRhi *rhi, QSize size, QRhiTexture::Format format) { return make(rhi, size, format, true); }

bool RhiTarget::make(QRhi *rhi, QSize size, QRhiTexture::Format format, bool mipped)
{
    size = size.expandedTo(QSize(1, 1));
    if (m_rhi == rhi && (mipped ? readyMipped(size, format) : ready(size, format))) return false;
    release();
    m_rhi = rhi;
    QRhiTexture::Flags flags = QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource;
    if (mipped) flags |= QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips;
    m_tex.reset(rhi->newTexture(format, size, 1, flags));
    if (!m_tex->create()) {
        m_tex.reset();
        return false;
    }
    m_rt.reset(rhi->newTextureRenderTarget({m_tex.get()}));
    m_rp.reset(m_rt->newCompatibleRenderPassDescriptor());
    m_rt->setRenderPassDescriptor(m_rp.get());
    if (!m_rt->create()) {
        release();
        return false;
    }
    m_size = size;
    m_format = format;
    m_mipped = mipped;
    videoBudget::addGpu(videoBudget::Gpu::Targets, bytes());
    return true;
}

QRhiTextureRenderTarget *RhiTarget::kept()
{
    if (!m_tex) return nullptr;
    if (!m_keep) {
        m_keep.reset(m_rhi->newTextureRenderTarget({m_tex.get()}, QRhiTextureRenderTarget::PreserveColorContents));
        m_keepRp.reset(m_keep->newCompatibleRenderPassDescriptor());
        m_keep->setRenderPassDescriptor(m_keepRp.get());
        if (!m_keep->create()) {
            m_keep.reset();
            m_keepRp.reset();
        }
    }
    return m_keep.get();
}

int RhiTarget::ensureMsaa(int samples)
{
    if (!m_tex || !m_rhi) return 1;
    int use = 1;
    for (const int s : m_rhi->supportedSampleCounts()) // the largest supported count not above the one asked
        if (s <= samples && s > use) use = s;
    if (use == m_samples && (use == 1 || m_msRt)) return use;
    m_msRt.reset();
    m_msRp.reset();
    m_msRb.reset();
    m_samples = 1;
    if (use <= 1) return 1;
    m_msRb.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::Color, m_size, use, {}, m_format));
    if (!m_msRb->create()) {
        m_msRb.reset();
        return 1;
    }
    QRhiColorAttachment att(m_msRb.get());
    att.setResolveTexture(m_tex.get());
    m_msRt.reset(m_rhi->newTextureRenderTarget({att}));
    m_msRp.reset(m_msRt->newCompatibleRenderPassDescriptor());
    m_msRt->setRenderPassDescriptor(m_msRp.get());
    if (!m_msRt->create()) {
        m_msRt.reset();
        m_msRp.reset();
        m_msRb.reset();
        return 1;
    }
    m_samples = use;
    return use;
}
