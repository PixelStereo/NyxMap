#include "ImageFile.h"
#include "Psd.h"

#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <cmath>
#ifdef PINOL_HAS_SVG
#include <QSvgRenderer>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

// The first picture of the file, to RGBA: 16 bits per channel when the source has more than 8 (or is float)
static QImage toImage(const AVFrame *f)
{
    const auto *desc = av_pix_fmt_desc_get(AVPixelFormat(f->format));
    if (!desc || f->width <= 0 || f->height <= 0) return {};
    const bool deep = desc->comp[0].depth > 8 || (desc->flags & AV_PIX_FMT_FLAG_FLOAT);
    QImage img(f->width, f->height, deep ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    if (img.isNull()) return {};
    SwsContext *sws = sws_getContext(f->width, f->height, AVPixelFormat(f->format), f->width, f->height,
                                     deep ? AV_PIX_FMT_RGBA64LE : AV_PIX_FMT_RGBA, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!sws) return {};
    uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    const int stride[4] = {int(img.bytesPerLine()), 0, 0, 0};
    const int rows = sws_scale(sws, f->data, f->linesize, 0, f->height, dst, stride);
    sws_freeContext(sws);
    if (rows != f->height) return {};
    // OpenEXR and the like are linear: said so, the image path converts it like an embedded profile
    if (f->color_trc == AVCOL_TRC_LINEAR) img.setColorSpace(QColorSpace::SRgbLinear);
    return img;
}

static QImage decodeWithFfmpeg(const QString &path)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, QFile::encodeName(path).constData(), nullptr, nullptr) < 0) return {};
    QImage out;
    const AVCodec *codec = nullptr;
    const int stream = avformat_find_stream_info(fmt, nullptr) >= 0
                           ? av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0) : -1;
    AVCodecContext *ctx = stream >= 0 && codec ? avcodec_alloc_context3(codec) : nullptr;
    if (ctx && avcodec_parameters_to_context(ctx, fmt->streams[stream]->codecpar) >= 0 && avcodec_open2(ctx, codec, nullptr) >= 0) {
        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        bool got = false;
        while (pkt && frame && !got && av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == stream && avcodec_send_packet(ctx, pkt) >= 0) got = avcodec_receive_frame(ctx, frame) >= 0;
            av_packet_unref(pkt);
        }
        if (frame && !got && avcodec_send_packet(ctx, nullptr) >= 0) got = avcodec_receive_frame(ctx, frame) >= 0;
        if (got) out = toImage(frame);
        av_frame_free(&frame);
        av_packet_free(&pkt);
    }
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return out;
}

bool isSvgFile(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("svg") || ext == QLatin1String("svgz");
}

#ifdef PINOL_HAS_SVG
// Its view box (else its declared size), scaled up to the wanted longer side, within the largest texture made here
static QSize svgSize(const QSvgRenderer &r, int longSide)
{
    QSizeF s = r.viewBoxF().size();
    if (s.width() <= 0 || s.height() <= 0) s = r.defaultSize();
    if (s.width() <= 0 || s.height() <= 0) return {};
    const double longest = std::max(s.width(), s.height());
    const double scale = std::min(std::max(1.0, longSide / longest), kMaxSvgSide / longest);
    return QSize(std::max(1, int(std::lround(s.width() * scale))), std::max(1, int(std::lround(s.height() * scale))));
}
#endif

QSize svgRenderSize(const QString &path, int svgLongSide)
{
#ifdef PINOL_HAS_SVG
    QSvgRenderer r(path);
    return r.isValid() ? svgSize(r, svgLongSide) : QSize();
#else
    (void)path;
    (void)svgLongSide;
    return {};
#endif
}

static QImage readSvg(const QString &path, int longSide, QString *err)
{
#ifdef PINOL_HAS_SVG
    QSvgRenderer r(path);
    const QSize size = r.isValid() ? svgSize(r, longSide) : QSize();
    if (!size.isValid()) {
        if (err) *err = QStringLiteral("Unreadable SVG drawing: ") + path;
        return {};
    }
    QImage img(size, QImage::Format_RGBA8888_Premultiplied);
    if (img.isNull()) return {};
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    r.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    p.end();
    img.setColorSpace(QColorSpace::SRgb); // SVG colours are sRGB
    return img.convertToFormat(QImage::Format_RGBA8888);
#else
    (void)longSide;
    if (err) *err = QStringLiteral("SVG drawings need Qt SVG, absent from this build: ") + path;
    return {};
#endif
}

QImage readImageFile(const QString &path, int psdLayer, QString *err, int svgLongSide)
{
    if (isSvgFile(path)) return readSvg(path, svgLongSide, err);
    if (isPsdFile(path)) {
        QImage img = readPsd(path, psdLayer, err);
        return img.isNull() && psdLayer < 0 ? decodeWithFfmpeg(path) : img;
    }
    QImage img(path);
    return img.isNull() ? decodeWithFfmpeg(path) : img;
}
