// Exercise borrowed upload memory without a physical hardware decoder. The linker substitutes
// only our transfer call; real AVFrame/AVBuffer ownership and layout construction remain in use.
#include "VideoDecoder.h"
#include "VideoDecoderInternal.h"
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <vector>

static const VideoLayout *expected;
static uint8_t *upload;
static int failures = 0, calls = 0;
static bool reject = false;
static void check(bool ok, const char *why)
{
    if (!ok) { ++failures; std::printf("FAIL %s\n", why); }
}

extern "C" int __wrap_av_hwframe_transfer_data(AVFrame *dst, const AVFrame *, int)
{
    ++calls;
    check(dst->buf[0] && dst->buf[0]->data == upload, "transfer uses the borrowed buffer");
    check(dst->buf[0] && dst->buf[0]->size == expected->totalBytes, "transfer buffer capacity");
    if (reject) return AVERROR(ENOSYS);
    for (int p = 0; p < expected->planeCount; ++p) {
        const auto &plane = expected->plane[p];
        check(dst->data[p] == upload + plane.offset, "transfer plane address");
        check(dst->linesize[p] == int(plane.rowBytes), "transfer plane stride");
        for (int y = 0; y < plane.height; ++y)
            std::memset(dst->data[p] + y * dst->linesize[p], 32 + p, plane.rowBytes);
    }
    return 0;
}

int main()
{
    AVFrame *hw = av_frame_alloc(), *dst = av_frame_alloc();
    hw->width = 128;
    hw->height = 64;
    hw->hw_frames_ctx = av_buffer_allocz(sizeof(AVHWFramesContext));
    auto *frames = reinterpret_cast<AVHWFramesContext *>(hw->hw_frames_ctx->data);
    frames->width = hw->width;
    frames->height = hw->height;
    for (AVPixelFormat fmt : {AV_PIX_FMT_NV12, AV_PIX_FMT_P010LE, AV_PIX_FMT_YUV420P, AV_PIX_FMT_BGRA}) {
        frames->sw_format = fmt;
        AVFrame probe = *hw;
        probe.format = fmt;
        const auto layout = VideoDecoder::layoutOf(&probe);
        check(bool(layout), "supported transfer layout");
        if (!layout) continue;
        expected = layout.get();
        std::vector<uint8_t> bytes(layout->totalBytes + 128, 0xcd);
        upload = bytes.data() + 64;
        for (bool failure : {false, true, false}) {
            reject = failure;
            check(videoDecoderTransferToUpload(dst, hw, *layout, upload) == !failure,
                  "transfer result propagates");
            check(!dst->buf[0] && !dst->data[0], "destination drops all borrowed references");
            for (int i = 0; i < 64; ++i) {
                check(bytes[size_t(i)] == 0xcd, "leading guard intact");
                check(bytes[bytes.size() - 1 - size_t(i)] == 0xcd, "trailing guard intact");
            }
            // Reuse the same upload mapping after success and failure. AVFrame cleanup must
            // never release the pool's allocation.
            if (!failure)
                for (int p = 0; p < layout->planeCount; ++p)
                    for (size_t i = 0; i < layout->plane[p].size; ++i)
                        check(upload[layout->plane[p].offset + i] == 32 + p, "plane written in place");
        }
        const int before = calls;
        frames->height += 16;
        check(!videoDecoderTransferToUpload(dst, hw, *layout, upload), "padded surfaces use fallback");
        frames->height -= 16;
        check(calls == before, "padded surface does not start a transfer");
    }
    av_frame_free(&dst);
    av_frame_free(&hw);
    std::printf("video transfer: %d failures, %d transfers\n", failures, calls);
    return failures ? 1 : 0;
}
