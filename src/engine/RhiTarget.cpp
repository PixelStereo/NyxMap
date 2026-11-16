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
    m_keep.reset();
    m_keepRp.reset();
    m_rt.reset();
    m_rp.reset();
    m_tex.reset(); // QRhi releases the native texture once no frame in flight uses it
    m_size = QSize();
}

bool RhiTarget::ensure(QRhi *rhi, QSize size, QRhiTexture::Format format)
{
    size = size.expandedTo(QSize(1, 1));
    if (m_rhi == rhi && ready(size, format)) return false;
    release();
    m_rhi = rhi;
    m_tex.reset(rhi->newTexture(format, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
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
