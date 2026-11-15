#pragma once
// Shared between the translation units of VideoDecoder (not part of its interface).

#include <QString>
#include <cstddef>
#include <cstdint>

struct AVFrame;
struct VideoLayout;

// Synchronous hardware readback into borrowed upload memory. dst must differ from hw.
// Clears dst on return; never owns ptr.
bool videoDecoderTransferToUpload(AVFrame *dst, const AVFrame *hw, const VideoLayout &layout, uint8_t *ptr);

extern "C" {
#include <libavutil/pixfmt.h>
}

// The YUV → RGB matrix of a colour space (3×3, row-major) and its name ("BT.709"…)
void videoDecoderYuvMatrix(AVColorSpace space, int height, float m[9], QString *name);
