#pragma once
// A video layer's hardware frames as QRhi textures, on the GPU all the way (render thread of that QRhi).
//  - macOS (Metal): each plane of VideoToolbox's IOSurface becomes an MTLTexture (CVMetalTextureCache) that the
//    QRhi texture adopts: no copy. The frames stay held while the GPU may still read them.
//  - Windows (Direct3D 11): the decoders decode on QRhi's own device (decoderDevice); each frame is copied on the
//    GPU from the decoder's texture array into this layer's texture, whose planes QRhi samples (one GPU copy, as Qt
//    Multimedia and others do: sampling the decoder's own textures is not allowed by every driver).
//  - elsewhere: not available (frames come through the CPU).
// The planes are described by the frame's VideoLayout (Mode::Yuv, one or two channels of 8 or 16 bits).

#include "VideoFrame.h"

#include <memory>

class QRhi;
class QRhiCommandBuffer;
class QRhiTexture;
struct AVBufferRef;

class RhiVideoSurface
{
public:
    explicit RhiVideoSurface(QRhi *rhi);
    ~RhiVideoSurface();
    RhiVideoSurface(const RhiVideoSurface &) = delete;
    RhiVideoSurface &operator=(const RhiVideoSurface &) = delete;

    // Hardware frames can be taken by this QRhi
    static bool available(QRhi *rhi);
    // The device the decoders must decode on for their frames to be taken (Windows, Direct3D 11: QRhi's, made safe
    // for the decoders' threads), a new reference; nullptr where they use their own (macOS) or none can be
    static AVBufferRef *decoderDevice(QRhi *rhi);

    // The frame's planes as this surface's textures, for the passes recorded after this call in `cb` (outside any
    // pass). False: not taken (the frame goes through the CPU instead).
    bool take(QRhiCommandBuffer *cb, const VideoFrame &frame);
    QRhiTexture *plane(int p) const; // nullptr beyond the layout's planes

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
