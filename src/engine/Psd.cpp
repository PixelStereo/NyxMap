#include "Psd.h"

#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QRgba64>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

// What is reproduced: the layers' pixels, their transparency, their opacity and their eye, and the groups' (their eye
// and their opacity). What is not: blend modes other than normal (composited as normal), clipping, layer and vector
// masks, layer effects, fill opacity, adjustment and fill layers, smart objects (their last rendered pixels are used).
// The file's own merged image, when it has one, is all of that as Photoshop drew it: "all layers" uses it.

namespace {

constexpr qint64 kMaxSide = 30000;            // the specification's limit for a .psd (larger .psb canvases are refused)
constexpr qint64 kMaxBytes = qint64(1) << 30; // one picture, or the planes of one layer, decoded: at most this

// Big-endian reads within [pos, end): one past the end fails the reader (ok false) and reads nothing outside
struct Reader {
    const uchar *base = nullptr;
    qint64 pos = 0, end = 0;
    bool ok = true;

    bool need(qint64 n)
    {
        if (!ok || n < 0 || n > end - pos) ok = false;
        return ok;
    }
    quint64 be(int n)
    {
        if (!need(n)) return 0;
        quint64 v = 0;
        for (int k = 0; k < n; ++k) v = v << 8 | base[pos + k];
        pos += n;
        return v;
    }
    quint8 u8() { return quint8(be(1)); }
    quint16 u16() { return quint16(be(2)); }
    quint32 u32() { return quint32(be(4)); }
    qint32 i32() { return qint32(u32()); }
    // A length: 8 bytes where a .psb has them; one no file can hold fails the next need()
    qint64 length(bool wide)
    {
        const quint64 v = be(wide ? 8 : 4);
        return v > quint64(end) ? -1 : qint64(v);
    }
    void skip(qint64 n)
    {
        if (need(n)) pos += n;
    }
    QByteArray tag()
    {
        if (!need(4)) return {};
        const QByteArray t(reinterpret_cast<const char *>(base + pos), 4);
        pos += 4;
        return t;
    }
    // After a block: its padding, up to the next signature (writers pad to 2 or 4 bytes)
    void align()
    {
        for (int k = 0; k < 3 && pos + 4 <= end; ++k) {
            if (!std::memcmp(base + pos, "8BIM", 4) || !std::memcmp(base + pos, "8B64", 4)) return;
            ++pos;
        }
    }
};

// Additional layer information whose length has 8 bytes in a .psb
bool wideKey(const QByteArray &key)
{
    static const char *const keys[] = {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
                                       "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
    return std::any_of(std::begin(keys), std::end(keys), [&](const char *k) { return key == k; });
}

struct Channel {
    int id = 0;          // 0.. colors, -1 transparency, -2 / -3 masks
    qint64 offset = -1;  // of its compression field in the file (-1: past its end)
    qint64 length = 0;   // with that field
};

struct Record {
    PsdLayerInfo info;
    std::vector<Channel> channels;
    int flags = 0;
    int section = 0;         // lsct: 1 / 2 a group (open / closed), 3 the end of one
    bool shown = true;       // with its groups'
    double opacity = 1;      // with its groups'
};

struct Document {
    bool psb = false;
    int channels = 0, width = 0, height = 0, depth = 0, mode = 0;
    QColorSpace profile;
    std::vector<Record> layers;
    bool mergedAlpha = false; // a negative layer count: the merged image's first extra channel is its transparency
    qint64 imageData = -1;    // the merged image's compression field
    QString layerError;       // the layer list could not be read (whole, or the pixels of the last ones)
};

bool fail(QString *err, const QString &m)
{
    if (err) *err = m;
    return false;
}

// The layer list (its records, then their channels' data), from the layer count on
void readLayerInfo(Reader r, Document &d)
{
    int count = qint16(r.u16());
    if (count < 0) {
        count = -count;
        d.mergedAlpha = true;
    }
    std::vector<Record> list;
    for (int k = 0; k < count && r.ok; ++k) {
        Record rec;
        const qint64 top = r.i32(), left = r.i32(), bottom = r.i32(), right = r.i32();
        const int channels = r.u16();
        if (!r.ok || bottom < top || right < left || bottom - top > 10 * kMaxSide || right - left > 10 * kMaxSide ||
            channels > 56) {
            d.layerError = QStringLiteral("Damaged layer record");
            return;
        }
        rec.info.rect = QRect(int(left), int(top), int(right - left), int(bottom - top));
        for (int c = 0; c < channels; ++c) {
            Channel ch;
            ch.id = qint16(r.u16());
            ch.length = r.length(d.psb);
            rec.channels.push_back(ch);
        }
        if (r.tag() != "8BIM") {
            d.layerError = QStringLiteral("Damaged layer record");
            return;
        }
        rec.info.blend = r.tag();
        rec.info.opacity = r.u8();
        r.u8(); // clipping
        rec.flags = r.u8();
        r.skip(1);
        rec.info.visible = !(rec.flags & 2);
        const qint64 extra = r.u32();
        if (!r.need(extra)) break;
        // Its mask and blending ranges (skipped), its name, then the additional information it has
        Reader x{r.base, r.pos, r.pos + extra};
        x.skip(x.u32());
        x.skip(x.u32());
        const int nameLength = x.u8();
        if (x.need(nameLength)) rec.info.name = QString::fromLatin1(reinterpret_cast<const char *>(x.base + x.pos), nameLength);
        x.skip(nameLength);
        x.skip((4 - (1 + nameLength) % 4) % 4); // padded to 4 bytes, with its length byte
        while (x.ok && x.pos + 12 <= x.end) {
            const QByteArray sig = x.tag();
            if (sig != "8BIM" && sig != "8B64") break;
            const QByteArray key = x.tag();
            const qint64 length = x.length(d.psb && wideKey(key));
            if (!x.need(length)) break;
            Reader b{x.base, x.pos, x.pos + length};
            if (key == "luni") { // its Unicode name (UTF-16, big-endian)
                const qint64 n = b.u32();
                if (b.need(n * 2)) {
                    QString name(int(n), Qt::Uninitialized);
                    for (qint64 c = 0; c < n; ++c) name[int(c)] = QChar(b.u16());
                    while (name.endsWith(QChar(0))) name.chop(1);
                    if (!name.isEmpty()) rec.info.name = name;
                }
            } else if (key == "lsct" || key == "lsdk") {
                rec.section = int(b.u32());
            }
            x.pos += length;
            x.align();
        }
        r.pos += extra;
        list.push_back(std::move(rec));
    }
    if (!r.ok) {
        d.layerError = QStringLiteral("The layer list is cut short (damaged file)");
        return;
    }
    // Their pixels, one layer after the other, channel after channel
    for (Record &rec : list)
        for (Channel &ch : rec.channels) {
            if (!r.need(ch.length)) break;
            ch.offset = r.pos;
            r.pos += ch.length;
        }
    if (!r.ok) d.layerError = QStringLiteral("The layers' pixels are cut short (damaged file)");

    // Groups, from the top of the list: a group's record comes above its layers, the end of it below them
    struct Open {
        QString name;
        bool shown;
        double opacity;
    };
    std::vector<Open> open;
    for (int k = int(list.size()) - 1; k >= 0; --k) {
        Record &rec = list[size_t(k)];
        QStringList path;
        for (const Open &o : open) path << o.name;
        const bool shown = open.empty() || open.back().shown;
        const double opacity = open.empty() ? 1.0 : open.back().opacity;
        rec.shown = shown && rec.info.visible;
        rec.opacity = opacity * rec.info.opacity / 255.0;
        rec.info.group = rec.section >= 1 && rec.section <= 3;
        if (rec.section == 3) {
            if (!open.empty()) open.pop_back();
            path << rec.info.name;
        } else {
            path << rec.info.name;
            if (rec.section == 1 || rec.section == 2) open.push_back({rec.info.name, rec.shown, rec.opacity});
        }
        rec.info.path = path.join(QStringLiteral(" / "));
    }
    d.layers = std::move(list);
}

bool parse(const uchar *data, qint64 size, Document &d, QString *err)
{
    Reader r{data, 0, size};
    if (r.tag() != "8BPS") return fail(err, QStringLiteral("Not a Photoshop file"));
    const int version = r.u16();
    if (version != 1 && version != 2) return fail(err, QStringLiteral("Unknown Photoshop file version %1").arg(version));
    d.psb = version == 2;
    r.skip(6);
    d.channels = r.u16();
    d.height = int(std::min<quint32>(r.u32(), 1u << 30));
    d.width = int(std::min<quint32>(r.u32(), 1u << 30));
    d.depth = r.u16();
    d.mode = r.u16();
    if (!r.ok) return fail(err, QStringLiteral("Photoshop file cut short"));
    if (d.channels < 1 || d.channels > 56) return fail(err, QStringLiteral("Damaged Photoshop file (%1 channels)").arg(d.channels));
    if (d.width < 1 || d.height < 1 || d.width > kMaxSide || d.height > kMaxSide)
        return fail(err, QStringLiteral("Unsupported Photoshop canvas size %1 × %2").arg(d.width).arg(d.height));
    r.skip(r.u32()); // color mode data (indexed, duotone)
    const qint64 resources = r.u32();
    if (!r.need(resources)) return fail(err, QStringLiteral("Photoshop file cut short"));
    // Image resources: only its ICC profile is used
    for (Reader x{data, r.pos, r.pos + resources}; x.ok && x.pos + 12 <= x.end;) {
        if (x.tag() != "8BIM") break;
        const int id = x.u16();
        const int nameLength = x.u8();
        x.skip(nameLength + ((nameLength + 1) % 2)); // padded to an even size, with its length byte
        const qint64 length = x.u32();
        if (!x.need(length)) break;
        if (id == 1039 && d.mode == 3 && length < (1 << 26))
            d.profile = QColorSpace::fromIccProfile(QByteArray(reinterpret_cast<const char *>(data + x.pos), int(length)));
        x.pos += length + (length % 2);
    }
    r.pos += resources;
    const qint64 section = r.length(d.psb);
    if (!r.need(section)) return fail(err, QStringLiteral("Photoshop file cut short"));
    const qint64 sectionEnd = r.pos + section;
    if (section > 0) {
        Reader s{data, r.pos, sectionEnd};
        const qint64 info = s.length(d.psb);
        if (s.need(info)) {
            if (info > 0) readLayerInfo(Reader{data, s.pos, s.pos + info}, d);
            s.pos += info;
            s.skip(s.u32()); // global layer mask
            // 16 and 32-bit files keep their layers in a block of their own, here
            while (s.ok && s.pos + 12 <= s.end) {
                const QByteArray sig = s.tag();
                if (sig != "8BIM" && sig != "8B64") break;
                const QByteArray key = s.tag();
                const qint64 length = s.length(d.psb && wideKey(key));
                if (!s.need(length)) break;
                if ((key == "Lr16" || key == "Lr32" || key == "Layr") && d.layers.empty() && d.layerError.isEmpty())
                    readLayerInfo(Reader{data, s.pos, s.pos + length}, d);
                s.pos += length;
                s.align();
            }
        } else {
            d.layerError = QStringLiteral("The layer list is cut short (damaged file)");
        }
    }
    r.pos = sectionEnd;
    if (r.pos + 2 <= size) d.imageData = r.pos;
    return true;
}

QString modeName(int mode)
{
    switch (mode) {
    case 0: return QStringLiteral("bitmap");
    case 2: return QStringLiteral("indexed");
    case 4: return QStringLiteral("CMYK");
    case 7: return QStringLiteral("multichannel");
    case 8: return QStringLiteral("duotone");
    case 9: return QStringLiteral("Lab");
    default: return QString::number(mode);
    }
}

// Its pixels can be read here: RGB or grayscale, 8 or 16 bits
bool readable(const Document &d, QString *err)
{
    if (d.mode != 1 && d.mode != 3)
        return fail(err, QStringLiteral("Photoshop file in an unsupported color mode (%1): only RGB and grayscale "
                                        "files are read layer by layer").arg(modeName(d.mode)));
    if (d.depth != 8 && d.depth != 16)
        return fail(err, QStringLiteral("%1-bit Photoshop files are not read layer by layer (8 and 16 bits are)").arg(d.depth));
    return true;
}

// PackBits: one row of `want` bytes from n bytes
bool unpackBits(const uchar *src, qint64 n, uchar *dst, qint64 want)
{
    qint64 i = 0, o = 0;
    while (o < want && i < n) {
        const int c = qint8(src[i++]);
        if (c >= 0) {
            const qint64 k = c + 1;
            if (i + k > n || o + k > want) return false;
            std::memcpy(dst + o, src + i, size_t(k));
            i += k;
            o += k;
        } else if (c != -128) {
            const qint64 k = 1 - c;
            if (i >= n || o + k > want) return false;
            std::memset(dst + o, src[i++], size_t(k));
            o += k;
        }
    }
    return o == want;
}

// `planes` planes of w × h samples (bps bytes each, big-endian) from a compression field and its data, `length`
// bytes in all: raw, RLE (the rows' byte counts first, every plane's), ZIP without or with prediction
bool decodePlanes(const uchar *p, qint64 length, int planes, int w, int h, int bps, bool psb,
                  std::vector<uchar> &out, QString *err)
{
    const qint64 row = qint64(w) * bps, rows = qint64(h) * planes, size = row * rows;
    if (size > kMaxBytes) return fail(err, QStringLiteral("Photoshop layer too large to be read"));
    if (size > 0 && length < 2) return fail(err, QStringLiteral("Photoshop file cut short"));
    out.assign(size_t(size), 0);
    if (size == 0) return true; // an empty layer: nothing to read (its field may even be missing)
    const int compression = p[0] << 8 | p[1];
    p += 2;
    length -= 2;
    switch (compression) {
    case 0:
        if (length < size) return fail(err, QStringLiteral("Photoshop file cut short"));
        std::memcpy(out.data(), p, size_t(size));
        return true;
    case 1: {
        const int width = psb ? 4 : 2;
        if (length < rows * width) return fail(err, QStringLiteral("Photoshop file cut short"));
        const uchar *src = p + rows * width;
        qint64 left = length - rows * width;
        for (qint64 y = 0; y < rows; ++y) {
            qint64 n = 0;
            for (int k = 0; k < width; ++k) n = n << 8 | p[y * width + k];
            if (n > left || !unpackBits(src, n, out.data() + y * row, row))
                return fail(err, QStringLiteral("Damaged RLE data in the Photoshop file"));
            src += n;
            left -= n;
        }
        return true;
    }
    case 2:
    case 3: {
        if (bps > 2) return fail(err, QStringLiteral("ZIP-compressed 32-bit Photoshop data is not supported"));
        // qUncompress: a zlib stream after its expected size (big-endian)
        QByteArray z(int(std::min<qint64>(length, 0x7fffffff - 4)) + 4, Qt::Uninitialized);
        for (int k = 0; k < 4; ++k) z[k] = char(quint32(size) >> (24 - 8 * k));
        std::memcpy(z.data() + 4, p, size_t(z.size() - 4));
        const QByteArray plain = qUncompress(z);
        if (plain.size() != size) return fail(err, QStringLiteral("Damaged ZIP data in the Photoshop file"));
        std::memcpy(out.data(), plain.constData(), size_t(size));
        if (compression == 3) // each sample is the difference from the one on its left
            for (qint64 y = 0; y < rows; ++y) {
                uchar *s = out.data() + y * row;
                if (bps == 1) {
                    for (int x = 1; x < w; ++x) s[x] = uchar(s[x] + s[x - 1]);
                } else {
                    for (int x = 1; x < w; ++x) {
                        const quint16 v = quint16((s[2 * x] << 8 | s[2 * x + 1]) + (s[2 * x - 2] << 8 | s[2 * x - 1]));
                        s[2 * x] = uchar(v >> 8);
                        s[2 * x + 1] = uchar(v);
                    }
                }
            }
        return true;
    }
    default:
        return fail(err, QStringLiteral("Unknown compression (%1) in the Photoshop file").arg(compression));
    }
}

// Pixels as planes: their colors (one for grayscale, three for RGB) and their transparency (none: opaque); a plane
// the pixels do not have is 0
struct Planes {
    int w = 0, h = 0, bps = 1;
    int colors = 3;
    const uchar *color[3] = {}, *alpha = nullptr;
    std::vector<uchar> store[4]; // what they point to, when decoded for them

    quint16 at(const uchar *p, qint64 i) const { return bps == 1 ? p[i] : quint16(p[2 * i] << 8 | p[2 * i + 1]); }
    // RGBA of sample i, 0..max
    void rgba(qint64 i, int max, quint16 c[4]) const
    {
        for (int k = 0; k < 3; ++k) {
            const uchar *p = color[colors == 3 ? k : 0];
            c[k] = p ? at(p, i) : 0;
        }
        c[3] = alpha ? at(alpha, i) : quint16(max);
    }
};

// The planes of a layer: its color channels and its transparency (masks and others left out)
bool layerPlanes(const uchar *data, const Document &d, const Record &rec, Planes &out, QString *err)
{
    out.w = rec.info.rect.width();
    out.h = rec.info.rect.height();
    out.bps = d.depth / 8;
    out.colors = d.mode == 3 ? 3 : 1;
    if (qint64(out.w) * out.h * out.bps * (out.colors + 1) > kMaxBytes)
        return fail(err, QStringLiteral("Photoshop layer too large to be read"));
    for (const Channel &ch : rec.channels) {
        const int k = ch.id == -1 ? 3 : ch.id >= 0 && ch.id < out.colors ? ch.id : -1;
        if (k < 0) continue;
        if (ch.offset < 0) return fail(err, QStringLiteral("The layers' pixels are cut short (damaged file)"));
        QString why;
        if (!decodePlanes(data + ch.offset, ch.length, 1, out.w, out.h, out.bps, d.psb, out.store[k], &why))
            return fail(err, QStringLiteral("Layer \"%1\": %2").arg(rec.info.name, why));
        (k == 3 ? out.alpha : out.color[k]) = out.store[k].data();
    }
    return true;
}

// A canvas-size picture, transparent, in the format of the file's depth
QImage canvas(const Document &d)
{
    if (qint64(d.width) * d.height * (d.depth == 16 ? 8 : 4) > kMaxBytes) return {};
    QImage img(d.width, d.height, d.depth == 16 ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    if (!img.isNull()) img.fill(Qt::transparent);
    return img;
}

void setPixel(QImage &img, int x, int y, const quint16 c[4])
{
    if (img.format() == QImage::Format_RGBA64) {
        reinterpret_cast<QRgba64 *>(img.scanLine(y))[x] = QRgba64::fromRgba64(c[0], c[1], c[2], c[3]);
    } else {
        uchar *p = img.scanLine(y) + 4 * x;
        for (int k = 0; k < 4; ++k) p[k] = uchar(c[k]);
    }
}

void pixel(const QImage &img, int x, int y, quint16 c[4])
{
    if (img.format() == QImage::Format_RGBA64) {
        const QRgba64 v = reinterpret_cast<const QRgba64 *>(img.constScanLine(y))[x];
        c[0] = v.red();
        c[1] = v.green();
        c[2] = v.blue();
        c[3] = v.alpha();
    } else {
        const uchar *p = img.constScanLine(y) + 4 * x;
        for (int k = 0; k < 4; ++k) c[k] = p[k];
    }
}

// The planes at the rect's place on the picture, at `opacity`: over what is there (straight alpha, normal
// blending), or in its place
void paint(QImage &img, const Planes &pl, const QRect &rect, double opacity, bool over)
{
    const int max = img.format() == QImage::Format_RGBA64 ? 65535 : 255;
    const QRect area = rect.intersected(img.rect());
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            quint16 s[4];
            pl.rgba(qint64(y - rect.top()) * pl.w + (x - rect.left()), max, s);
            const double sa = s[3] * opacity / max;
            if (!over) {
                s[3] = quint16(std::lround(sa * max));
                setPixel(img, x, y, s);
                continue;
            }
            if (sa <= 0) continue;
            quint16 c[4];
            pixel(img, x, y, c);
            const double da = c[3] / double(max), a = sa + da * (1 - sa);
            for (int k = 0; k < 3; ++k) c[k] = quint16(std::lround((s[k] * sa + c[k] * da * (1 - sa)) / a));
            c[3] = quint16(std::lround(a * max));
            setPixel(img, x, y, c);
        }
}

// The file's merged image; uniform (one color all over) when the file was saved without it
bool mergedImage(const uchar *data, qint64 size, const Document &d, QImage &img, bool *uniform, QString *err)
{
    if (d.imageData < 0) return fail(err, QStringLiteral("The Photoshop file has no merged image"));
    Planes pl;
    pl.w = d.width;
    pl.h = d.height;
    pl.bps = d.depth / 8;
    pl.colors = d.mode == 3 ? 3 : 1;
    std::vector<uchar> &all = pl.store[0];
    if (!decodePlanes(data + d.imageData, size - d.imageData, d.channels, pl.w, pl.h, pl.bps, d.psb, all, err)) return false;
    const qint64 plane = qint64(pl.w) * pl.h * pl.bps;
    for (int k = 0; k < pl.colors && k < d.channels; ++k) pl.color[k] = all.data() + k * plane;
    if (d.mergedAlpha && d.channels > pl.colors) pl.alpha = all.data() + pl.colors * plane;
    img = canvas(d);
    if (img.isNull()) return fail(err, QStringLiteral("Photoshop picture too large"));
    paint(img, pl, img.rect(), 1.0, false);
    const int px = img.format() == QImage::Format_RGBA64 ? 8 : 4, bpl = img.width() * px;
    *uniform = true;
    for (int y = 0; y < img.height() && *uniform; ++y) {
        const uchar *line = img.constScanLine(y);
        for (int x = 0; x < bpl && *uniform; x += px) *uniform = !std::memcmp(line + x, img.constBits(), size_t(px));
    }
    return true;
}

// The visible layers composited, bottom to top
bool composite(const uchar *data, const Document &d, QImage &img, QString *err)
{
    img = canvas(d);
    if (img.isNull()) return fail(err, QStringLiteral("Photoshop picture too large"));
    for (const Record &rec : d.layers) {
        // Pixels that do not show as they are (adjustment layers…): bit 4 says so where bit 3 says it is meaningful
        const bool irrelevant = (rec.flags & 0x18) == 0x18;
        if (rec.info.group || !rec.shown || irrelevant || rec.opacity <= 0 || rec.info.rect.isEmpty()) continue;
        Planes pl;
        if (!layerPlanes(data, d, rec, pl, err)) return false;
        paint(img, pl, rec.info.rect, rec.opacity, true);
    }
    return true;
}

// The file's bytes: mapped, or read when it cannot be
struct File {
    QFile file;
    QByteArray bytes;
    const uchar *data = nullptr;
    qint64 size = 0;

    bool open(const QString &path, QString *err)
    {
        file.setFileName(path);
        if (!file.open(QIODevice::ReadOnly)) return fail(err, QStringLiteral("Cannot open ") + path);
        size = file.size();
        data = file.map(0, size);
        if (!data) {
            bytes = file.readAll();
            data = reinterpret_cast<const uchar *>(bytes.constData());
            size = bytes.size();
        }
        return true;
    }
};

} // namespace

bool isPsdFile(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("psd") || ext == QLatin1String("psb");
}

QVector<PsdLayerInfo> psdLayers(const QString &file, QString *err, QSize *size)
{
    File f;
    Document d;
    if (!f.open(file, err) || !parse(f.data, f.size, d, err) || !readable(d, err)) return {};
    if (size) *size = QSize(d.width, d.height);
    if (d.layers.empty() && !d.layerError.isEmpty()) {
        if (err) *err = d.layerError;
        return {};
    }
    QVector<PsdLayerInfo> out;
    for (const Record &rec : d.layers) out.append(rec.info);
    return out;
}

QImage readPsd(const QString &file, int layer, QString *err)
{
    File f;
    Document d;
    if (!f.open(file, err) || !parse(f.data, f.size, d, err) || !readable(d, err)) return {};
    QImage img;
    if (layer >= 0) {
        if (layer >= int(d.layers.size())) {
            fail(err, d.layerError.isEmpty() ? QStringLiteral("The Photoshop file has no layer %1").arg(layer) : d.layerError);
            return {};
        }
        const Record &rec = d.layers[size_t(layer)];
        if (rec.info.group) {
            fail(err, QStringLiteral("\"%1\" is a group, not a layer").arg(rec.info.name));
            return {};
        }
        Planes pl;
        img = canvas(d);
        if (img.isNull()) {
            fail(err, QStringLiteral("Photoshop picture too large"));
            return {};
        }
        if (!layerPlanes(f.data, d, rec, pl, err)) return {};
        paint(img, pl, rec.info.rect, rec.info.opacity / 255.0, false);
    } else {
        // Its merged image, unless it is missing, damaged, or blank where the file has layers to show
        bool uniform = false;
        QString why;
        const bool merged = mergedImage(f.data, f.size, d, img, &uniform, &why);
        if (!merged || (uniform && !d.layers.empty())) {
            QImage own;
            if (!d.layers.empty() && composite(f.data, d, own, &why)) img = own;
            else if (!merged) {
                fail(err, why);
                return {};
            }
        }
    }
    if (d.profile.isValid()) img.setColorSpace(d.profile);
    return img;
}
