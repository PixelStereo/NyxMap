#pragma once
// A test reference shared by the video tests: FFmpeg's own decoding of a file's first frame, converted on the CPU.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <QImage>
#include <QString>

// The first frame as FFmpeg decodes it, converted by swscale with the matrix and range the GPU path takes,
// chroma interpolated. Top row first, RGBA.
inline QImage ffmpegReference(const QString &path)
{
    AVFormatContext *fmt = nullptr;
    QImage out;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) return out;
    avformat_find_stream_info(fmt, nullptr);
    const AVCodec *dec = nullptr;
    const int s = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (s < 0 || !dec) {
        avformat_close_input(&fmt);
        return out;
    }
    AVCodecContext *c = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(c, fmt->streams[s]->codecpar);
    avcodec_open2(c, dec, nullptr);
    AVFrame *fr = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    bool got = false;
    while (!got && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == s && avcodec_send_packet(c, pkt) >= 0) got = avcodec_receive_frame(c, fr) == 0;
        av_packet_unref(pkt);
    }
    if (!got) {
        avcodec_send_packet(c, nullptr);
        got = avcodec_receive_frame(c, fr) == 0;
    }
    if (got) {
        const int w = fr->width, h = fr->height;
        SwsContext *sws = sws_getContext(w, h, AVPixelFormat(fr->format), w, h, AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP, nullptr,
                                         nullptr, nullptr);
        int cs = SWS_CS_ITU601;
        if (fr->colorspace == AVCOL_SPC_BT709 || (fr->colorspace == AVCOL_SPC_UNSPECIFIED && h >= 720)) cs = SWS_CS_ITU709;
        if (fr->colorspace == AVCOL_SPC_BT2020_NCL) cs = SWS_CS_BT2020;
        const AVPixelFormat pf = AVPixelFormat(fr->format);
        const bool full = fr->color_range == AVCOL_RANGE_JPEG || pf == AV_PIX_FMT_YUVJ420P || pf == AV_PIX_FMT_YUVJ422P
                          || pf == AV_PIX_FMT_YUVJ444P;
        int *inv, *tbl, srcRange, dstRange, bri, con, sat;
        sws_getColorspaceDetails(sws, &inv, &srcRange, &tbl, &dstRange, &bri, &con, &sat);
        sws_setColorspaceDetails(sws, sws_getCoefficients(cs), full ? 1 : srcRange, tbl, 1, bri, con, sat);
        out = QImage(w, h, QImage::Format_RGBA8888);
        uint8_t *dst[4] = {out.bits(), nullptr, nullptr, nullptr};
        int stride[4] = {int(out.bytesPerLine()), 0, 0, 0};
        sws_scale(sws, fr->data, fr->linesize, 0, h, dst, stride);
        sws_freeContext(sws);
    }
    av_packet_free(&pkt);
    av_frame_free(&fr);
    avcodec_free_context(&c);
    avformat_close_input(&fmt);
    return out;
}

