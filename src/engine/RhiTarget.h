#pragma once
// The QRhi renderer's pictures drawn into (the port of RenderTarget), render thread of their QRhi.
// A texture, its render target and pass descriptor. Its first row holds the picture's bottom (RhiDevice.h).
// Two render targets over the same texture: one cleared when a pass begins, one that keeps what it holds (points
// drawn over a picture). Counted in the graphics memory report (VideoBudget.h).

#include <QSize>
#include <memory>
#include <rhi/qrhi.h>

class RhiTarget
{
public:
    RhiTarget() = default;
    ~RhiTarget() { release(); }
    RhiTarget(const RhiTarget &) = delete;
    RhiTarget &operator=(const RhiTarget &) = delete;
    RhiTarget(RhiTarget &&o) noexcept;
    RhiTarget &operator=(RhiTarget &&o) noexcept;

    // (Re)made when the size or format changes. Returns true when made now: its contents are undefined until a
    // pass clears it. False with ready() false: it could not be made (the previous one is gone).
    bool ensure(QRhi *rhi, QSize size, QRhiTexture::Format format = QRhiTexture::RGBA8);
    bool ready(QSize size, QRhiTexture::Format format) const { return m_tex && m_size == size && m_format == format; }
    bool ready() const { return bool(m_tex); }
    void release();

    QRhiTexture *texture() const { return m_tex.get(); }
    QRhiTextureRenderTarget *cleared() const { return m_rt.get(); }  // a pass on it begins cleared
    QRhiTextureRenderTarget *kept();                                   // a pass on it begins with what it holds
    QRhiRenderPassDescriptor *passDescriptor() const { return m_rp.get(); } // compatible with both
    QSize size() const { return m_size; }
    int width() const { return m_size.width(); }
    int height() const { return m_size.height(); }
    QRhiTexture::Format format() const { return m_format; }
    size_t bytes() const;

private:
    QRhi *m_rhi = nullptr;
    QSize m_size;
    QRhiTexture::Format m_format = QRhiTexture::RGBA8;
    std::unique_ptr<QRhiTexture> m_tex;
    std::unique_ptr<QRhiTextureRenderTarget> m_rt, m_keep;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rp, m_keepRp;
};
