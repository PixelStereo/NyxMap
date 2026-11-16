// D3D11VA's frames as QRhi textures, one GPU copy (RhiVideoSurface.h), Windows
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "RhiVideoSurface.h"

#include <rhi/qrhi.h>

#include <d3d10_1.h> // must precede d3d10.h (also pulled in by the D3D11 headers)
#include <d3d10.h>
#include <d3d11.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/pixfmt.h>
}

#include <algorithm>
#include <mutex>

struct RhiVideoSurface::Impl {
    QRhi *rhi = nullptr;
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    // This layer's copy of the frame, in the decoder's format (NV12, P010…): its planes sampled by QRhi. One is
    // enough: Direct3D 11 orders the copy after the reads of the previous frame on the same context.
    ID3D11Texture2D *copy = nullptr;
    D3D11_TEXTURE2D_DESC copyDesc = {};
    std::unique_ptr<QRhiTexture> planes[4];
    void releaseCopy()
    {
        for (auto &p : planes) p.reset();
        if (copy) copy->Release();
        copy = nullptr;
        copyDesc = {};
    }
};

RhiVideoSurface::RhiVideoSurface(QRhi *rhi) : d(std::make_unique<Impl>())
{
    d->rhi = rhi;
    if (!available(rhi)) return;
    auto *handles = static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
    d->device = static_cast<ID3D11Device *>(handles->dev);
    d->context = static_cast<ID3D11DeviceContext *>(handles->context);
}

RhiVideoSurface::~RhiVideoSurface() { d->releaseCopy(); }

bool RhiVideoSurface::available(QRhi *rhi)
{
    return rhi && rhi->backend() == QRhi::D3D11 && rhi->nativeHandles()
           && static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles())->dev;
}

// One device context per QRhi, kept for the process's life (decoders may outlive a layer)
AVBufferRef *RhiVideoSurface::decoderDevice(QRhi *rhi)
{
    if (!available(rhi)) return nullptr;
    static std::mutex mutex;
    static QRhi *made = nullptr;
    static AVBufferRef *ref = nullptr;
    std::lock_guard<std::mutex> lk(mutex);
    if (made == rhi && ref) return av_buffer_ref(ref);
    av_buffer_unref(&ref);
    made = nullptr;
    auto *handles = static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
    auto *device = static_cast<ID3D11Device *>(handles->dev);
    auto *context = static_cast<ID3D11DeviceContext *>(handles->context);
    // The decoders' threads and the render thread share the immediate context: Direct3D serializes them
    ID3D10Multithread *mt = nullptr;
    if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D10Multithread), reinterpret_cast<void **>(&mt))) && mt) {
        mt->SetMultithreadProtected(TRUE);
        mt->Release();
    }
    AVBufferRef *dev = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    if (!dev) return nullptr;
    auto *hw = reinterpret_cast<AVD3D11VADeviceContext *>(reinterpret_cast<AVHWDeviceContext *>(dev->data)->hwctx);
    device->AddRef();
    context->AddRef();
    hw->device = device;
    hw->device_context = context;
    if (av_hwdevice_ctx_init(dev) < 0) {
        av_buffer_unref(&dev);
        return nullptr;
    }
    ref = dev;
    made = rhi;
    return av_buffer_ref(ref);
}

bool RhiVideoSurface::take(QRhiCommandBuffer *cb, const VideoFrame &frame)
{
    if (!d->device || !cb || !frame.layout || !frame.layout->surface || !frame.av || frame.av->format != AV_PIX_FMT_D3D11)
        return false;
    const VideoLayout &L = *frame.layout;
    auto *source = reinterpret_cast<ID3D11Texture2D *>(frame.av->data[0]);
    const UINT slice = UINT(reinterpret_cast<intptr_t>(frame.av->data[1]));
    if (!source || L.planeCount > 2) return false;
    D3D11_TEXTURE2D_DESC sd = {};
    source->GetDesc(&sd);
    const UINT w = UINT((L.width + 1) & ~1), h = UINT((L.height + 1) & ~1); // 4:2:0 formats: even sizes
    if (!d->copy || d->copyDesc.Format != sd.Format || d->copyDesc.Width != w || d->copyDesc.Height != h) {
        d->releaseCopy();
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = sd.Format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(d->device->CreateTexture2D(&desc, nullptr, &d->copy)) || !d->copy) {
            d->copy = nullptr;
            return false;
        }
        d->copyDesc = desc;
        // Each plane sampled through its own view: R8/R16 the luma, RG8/RG16 the interleaved chroma
        for (int p = 0; p < L.planeCount; ++p) {
            const VideoLayout::Plane &pl = L.plane[p];
            const bool wide = pl.texels == VideoLayout::Texels::U16;
            if (pl.channels > 2 || (pl.texels != VideoLayout::Texels::U8 && !wide)) {
                d->releaseCopy();
                return false;
            }
            const QRhiTexture::Format qf = pl.channels == 1 ? (wide ? QRhiTexture::R16 : QRhiTexture::R8)
                                                            : (wide ? QRhiTexture::RG16 : QRhiTexture::RG8);
            d->planes[p].reset(d->rhi->newTexture(qf, QSize(pl.width, pl.height)));
            if (!d->planes[p]->createFrom({quint64(quintptr(d->copy)), 0})) {
                d->releaseCopy();
                return false;
            }
        }
    }
    // The copy, ordered with QRhi's own commands
    cb->beginExternal();
    D3D11_BOX box = {0, 0, 0, (std::min)(w, sd.Width), (std::min)(h, sd.Height), 1};
    d->context->CopySubresourceRegion(d->copy, 0, 0, 0, 0, source, slice, &box);
    cb->endExternal();
    return true;
}

QRhiTexture *RhiVideoSurface::plane(int p) const { return p >= 0 && p < 4 ? d->planes[p].get() : nullptr; }
