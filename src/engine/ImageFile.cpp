#include "ImageFile.h"
#include "Psd.h"

#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <cmath>
#include <cstring>
#include <functional>
#ifdef NYXMAP_HAS_SVG
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

// The file's first picture, handed to `use` (FFmpeg)
static void firstFrame(const QString &path, const std::function<void(const AVFrame *)> &use)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, QFile::encodeName(path).constData(), nullptr, nullptr) < 0) return;
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
        if (got) use(frame);
        av_frame_free(&frame);
        av_packet_free(&pkt);
    }
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
}

static QImage decodeWithFfmpeg(const QString &path)
{
    QImage out;
    firstFrame(path, [&](const AVFrame *f) { out = toImage(f); });
    return out;
}

bool isHdrImageFile(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("hdr") || ext == QLatin1String("exr");
}

bool readHdrPixels(const QString &path, std::vector<float> *rgba, QSize *size, int maxWidth)
{
    if (!isHdrImageFile(path)) return false;
    bool ok = false;
    firstFrame(path, [&](const AVFrame *f) {
        if (f->width <= 0 || f->height <= 0) return;
        const int fw = f->width, fh = f->height;
        // Its planes as floats (G, B, R, A): as decoded when they are (swscale clips floats to 1 on the way), else
        // converted
        std::vector<float> planes(size_t(fw) * fh * 4, 1.0f);
        auto plane = [&](int p) { return planes.data() + size_t(p) * fw * fh; };
        const AVPixelFormat format = AVPixelFormat(f->format);
        // Half floats (an EXR's usual): known by name, an older FFmpeg has no such formats
        static const AVPixelFormat half3 = av_get_pix_fmt("gbrpf16le"), half4 = av_get_pix_fmt("gbrapf16le");
        if (format != AV_PIX_FMT_NONE && (format == half3 || format == half4)) {
            auto toFloat = [](uint16_t h) { // IEEE 754 half to float
                const uint32_t sign = uint32_t(h & 0x8000) << 16, exp = (h >> 10) & 0x1f, mant = h & 0x3ff;
                uint32_t bits;
                if (exp == 0) {
                    if (mant == 0) bits = sign;
                    else { // subnormal: normalized here
                        int e = -1;
                        uint32_t m = mant;
                        do { ++e; m <<= 1; } while (!(m & 0x400));
                        bits = sign | uint32_t(127 - 15 - e) << 23 | (m & 0x3ff) << 13;
                    }
                } else if (exp == 31) {
                    bits = sign | 0x7f800000 | mant << 13;
                } else {
                    bits = sign | (exp + 127 - 15) << 23 | mant << 13;
                }
                float out;
                std::memcpy(&out, &bits, sizeof out);
                return out;
            };
            const int count = format == half4 ? 4 : 3;
            for (int p = 0; p < count; ++p)
                for (int y = 0; y < fh; ++y) {
                    const auto *row = reinterpret_cast<const uint16_t *>(f->data[p] + size_t(y) * f->linesize[p]);
                    float *out = plane(p) + size_t(y) * fw;
                    for (int x = 0; x < fw; ++x) out[x] = toFloat(row[x]);
                }
        } else if (format == AV_PIX_FMT_GBRPF32LE || format == AV_PIX_FMT_GBRAPF32LE) {
            const int count = format == AV_PIX_FMT_GBRAPF32LE ? 4 : 3;
            for (int p = 0; p < count; ++p)
                for (int y = 0; y < fh; ++y)
                    std::memcpy(plane(p) + size_t(y) * fw, f->data[p] + size_t(y) * f->linesize[p], size_t(fw) * sizeof(float));
        } else {
            SwsContext *sws = sws_getContext(fw, fh, format, fw, fh, AV_PIX_FMT_GBRPF32LE, SWS_POINT, nullptr, nullptr, nullptr);
            if (!sws) return;
            uint8_t *dst[4] = {};
            int stride[4] = {};
            for (int p = 0; p < 3; ++p) {
                dst[p] = reinterpret_cast<uint8_t *>(plane(p));
                stride[p] = fw * int(sizeof(float));
            }
            const int rows = sws_scale(sws, f->data, f->linesize, 0, fh, dst, stride);
            sws_freeContext(sws);
            if (rows != fh) return;
        }
        // Scaled down to maxWidth (an average of the pixels each one covers), interleaved RGBA, its first row at the
        // bottom (as the layers' pictures in OpenGL)
        const int w = std::min(fw, maxWidth), h = std::max(1, int(std::lround(double(fh) * w / fw)));
        rgba->assign(size_t(w) * h * 4, 0.0f);
        static const int order[4] = {2, 0, 1, 3}; // R, G, B, A from the planes G, B, R, A
        for (int y = 0; y < h; ++y) {
            const int y0 = y * fh / h, y1 = std::max(y0 + 1, (y + 1) * fh / h);
            float *out = rgba->data() + size_t(h - 1 - y) * w * 4;
            for (int x = 0; x < w; ++x) {
                const int x0 = x * fw / w, x1 = std::max(x0 + 1, (x + 1) * fw / w);
                for (int c = 0; c < 4; ++c) {
                    double sum = 0;
                    const float *p = plane(order[c]);
                    for (int yy = y0; yy < y1; ++yy)
                        for (int xx = x0; xx < x1; ++xx) sum += p[size_t(yy) * fw + xx];
                    out[4 * x + c] = float(sum / ((y1 - y0) * (x1 - x0)));
                }
            }
        }
        *size = QSize(w, h);
        ok = true;
    });
    return ok;
}

bool isSvgFile(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("svg") || ext == QLatin1String("svgz");
}

#ifdef NYXMAP_HAS_SVG
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
#ifdef NYXMAP_HAS_SVG
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
#ifdef NYXMAP_HAS_SVG
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
