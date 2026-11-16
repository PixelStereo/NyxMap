// VideoToolbox's frames as QRhi textures with no copy (RhiVideoSurface.h), macOS
#include "RhiVideoSurface.h"

#include <rhi/qrhi.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>

#include <array>

struct RhiVideoSurface::Impl {
    QRhi *rhi = nullptr;
    CVMetalTextureCacheRef cache = nullptr;
    // A frame and its planes' Metal textures, held while the GPU may still read them: QRhi does not own a texture
    // it adopted, and has at most two frames in flight
    struct Held {
        AVFrame *frame = nullptr;
        CVMetalTextureRef tex[4] = {nullptr, nullptr, nullptr, nullptr};
        void release()
        {
            for (CVMetalTextureRef &t : tex)
                if (t) {
                    CFRelease(t);
                    t = nullptr;
                }
            if (frame) av_frame_free(&frame);
        }
    };
    std::array<Held, 3> held;
    size_t next = 0;
    std::unique_ptr<QRhiTexture> planes[4];
};

RhiVideoSurface::RhiVideoSurface(QRhi *rhi) : d(std::make_unique<Impl>())
{
    d->rhi = rhi;
    if (!available(rhi)) return;
    auto *handles = static_cast<const QRhiMetalNativeHandles *>(rhi->nativeHandles());
    id<MTLDevice> device = (id<MTLDevice>)handles->dev;
    CVMetalTextureCacheRef cache = nullptr;
    if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr, &cache) == kCVReturnSuccess) d->cache = cache;
}

RhiVideoSurface::~RhiVideoSurface()
{
    for (auto &p : d->planes) p.reset();
    for (Impl::Held &h : d->held) h.release();
    if (d->cache) {
        CVMetalTextureCacheFlush(d->cache, 0);
        CFRelease(d->cache);
    }
}

bool RhiVideoSurface::available(QRhi *rhi) { return rhi && rhi->backend() == QRhi::Metal; }

AVBufferRef *RhiVideoSurface::decoderDevice(QRhi *) { return nullptr; } // VideoToolbox's surfaces are shared as they are

bool RhiVideoSurface::take(QRhiCommandBuffer *, const VideoFrame &frame)
{
    if (!d->cache || !frame.layout || !frame.layout->surface || !frame.av || frame.av->format != AV_PIX_FMT_VIDEOTOOLBOX)
        return false;
    const VideoLayout &L = *frame.layout;
    auto pixels = reinterpret_cast<CVPixelBufferRef>(frame.av->data[3]);
    if (!pixels || L.planeCount > 4) return false;
    Impl::Held &slot = d->held[d->next];
    d->next = (d->next + 1) % d->held.size();
    slot.release();
    for (int p = 0; p < L.planeCount; ++p) {
        const VideoLayout::Plane &pl = L.plane[p];
        const bool wide = pl.texels == VideoLayout::Texels::U16;
        if (pl.channels > 2 || (pl.texels != VideoLayout::Texels::U8 && !wide)) return false;
        const MTLPixelFormat mf = pl.channels == 1 ? (wide ? MTLPixelFormatR16Unorm : MTLPixelFormatR8Unorm)
                                                   : (wide ? MTLPixelFormatRG16Unorm : MTLPixelFormatRG8Unorm);
        const QRhiTexture::Format qf = pl.channels == 1 ? (wide ? QRhiTexture::R16 : QRhiTexture::R8)
                                                        : (wide ? QRhiTexture::RG16 : QRhiTexture::RG8);
        if (CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, d->cache, pixels, nullptr, mf, size_t(pl.width),
                                                      size_t(pl.height), size_t(p), &slot.tex[p]) != kCVReturnSuccess)
            return false;
        id<MTLTexture> mt = CVMetalTextureGetTexture(slot.tex[p]);
        d->planes[p].reset(d->rhi->newTexture(qf, QSize(pl.width, pl.height)));
        if (!mt || !d->planes[p]->createFrom({quint64(quintptr((__bridge void *)mt)), 0})) return false;
    }
    for (int p = L.planeCount; p < 4; ++p) d->planes[p].reset();
    slot.frame = av_frame_clone(frame.av); // its IOSurface stays the decoder's until released
    return true;
}

QRhiTexture *RhiVideoSurface::plane(int p) const { return p >= 0 && p < 4 ? d->planes[p].get() : nullptr; }
