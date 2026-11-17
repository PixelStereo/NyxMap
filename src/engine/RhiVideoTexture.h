#pragma once
// A video layer's picture through QRhi (render thread): the port of VideoTexture.
// The decoder's frames arrive as they were decoded — YUV planes, RGB, grey, or HAP's compressed blocks — or stay on
// the GPU (RhiVideoSurface: VideoToolbox, D3D11 on the renderer's device). Their planes become textures, and one
// pass turns them into the RGBA picture the layer samples: YUV matrix and range, bit depth, chroma siting, scaled
// YCoCg, alpha. The picture is stored as every NyxMap picture is: its bottom in the first row (RhiDevice.h).
// Planes of three channels (RGB 24 and 48 bits) and of four 16-bit channels, which no QRhi texture format holds, go
// as one channel three or four times as wide, read texel by texel.

#include "VideoFrame.h"

#include <memory>

class QRhi;
class QRhiCommandBuffer;
class QRhiTexture;

class RhiVideoTexture
{
public:
    explicit RhiVideoTexture(QRhi *rhi);
    ~RhiVideoTexture();
    RhiVideoTexture(const RhiVideoTexture &) = delete;
    RhiVideoTexture &operator=(const RhiVideoTexture &) = delete;

    // The frame becomes the picture: its planes uploaded (or taken on the GPU) and converted, recorded in `cb`
    // outside any pass. False: not converted (an unsupported layout, a texture the GPU refused); the picture stays
    // the last one.
    bool upload(QRhiCommandBuffer *cb, const VideoFrame &frame);

    QRhiTexture *texture() const; // RGBA8, the frame's size; nullptr before the first picture
    int width() const;
    int height() const;
    uint64_t uploads() const;        // frames shown so far
    uint64_t surfaceUploads() const; // of which taken on the GPU (no upload)

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
