#include "Commands.h"
#include "Engine.h"
#include "ImageFile.h"
#include "Psd.h"
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QUndoStack>
#include <cstdio>

// Photoshop files written here, as the specification lays them out: a layered RGB file, its channels raw, RLE and
// ZIP (with and without prediction), a group, and its merged image
namespace {
enum Compression { Raw = 0, Rle = 1, Zip = 2, ZipPredicted = 3 };

struct TestLayer {
    QString name, unicodeName;
    QRect rect;
    QColor color;
    bool hidden = false;
    int section = 0; // lsct: 1 a group, 3 the end of one
    Compression compression = Raw;
};

void be16(QByteArray &b, int v) { b.append(char(v >> 8)).append(char(v)); }
void be32(QByteArray &b, qint64 v)
{
    for (int k = 3; k >= 0; --k) b.append(char(v >> (8 * k)));
}

// PackBits: runs of one byte repeated, the rest as literals
QByteArray packBits(const QByteArray &row)
{
    QByteArray out;
    int i = 0;
    while (i < row.size()) {
        int run = 1;
        while (i + run < row.size() && run < 128 && row[i + run] == row[i]) ++run;
        if (run >= 2) {
            out.append(char(1 - run)).append(row[i]);
            i += run;
            continue;
        }
        int literal = 1;
        while (i + literal < row.size() && literal < 128 &&
               !(i + literal + 1 < row.size() && row[i + literal] == row[i + literal + 1]))
            ++literal;
        out.append(char(literal - 1)).append(row.mid(i, literal));
        i += literal;
    }
    return out;
}

// One color everywhere: w × h samples of `bytes` bytes, big-endian
QByteArray plane(int w, int h, int bytes, int value)
{
    QByteArray p;
    for (int i = 0; i < w * h; ++i) {
        if (bytes == 1) p.append(char(value));
        else be16(p, value * 257);
    }
    return p;
}

// Planes after their compression field, as a layer's channel or the merged image keeps them
QByteArray encode(const QVector<QByteArray> &planes, int w, int h, int bytes, Compression c)
{
    QByteArray out;
    be16(out, c);
    const int row = w * bytes;
    if (c == Raw) {
        for (const QByteArray &p : planes) out += p;
    } else if (c == Rle) {
        QVector<QByteArray> rows;
        for (const QByteArray &p : planes)
            for (int y = 0; y < h; ++y) rows << packBits(p.mid(y * row, row));
        for (const QByteArray &r : rows) be16(out, int(r.size()));
        for (const QByteArray &r : rows) out += r;
    } else {
        QByteArray all;
        for (QByteArray p : planes) {
            if (c == ZipPredicted) // each sample as the difference from the one on its left
                for (int y = 0; y < h; ++y)
                    for (int x = w - 1; x > 0; --x) {
                        char *s = p.data() + y * row;
                        if (bytes == 1) {
                            s[x] = char(uchar(s[x]) - uchar(s[x - 1]));
                        } else {
                            const int v = (uchar(s[2 * x]) << 8 | uchar(s[2 * x + 1])) -
                                          (uchar(s[2 * x - 2]) << 8 | uchar(s[2 * x - 1]));
                            s[2 * x] = char(v >> 8);
                            s[2 * x + 1] = char(v);
                        }
                    }
            all += p;
        }
        out += qCompress(all).mid(4); // a zlib stream, without Qt's size in front
    }
    return out;
}

QByteArray writePsd(int width, int height, int depth, const QVector<TestLayer> &layers, bool blankMerged)
{
    const int bytes = depth / 8;
    QByteArray f("8BPS");
    be16(f, 1);
    f.append(6, '\0');
    be16(f, 3);
    be32(f, height);
    be32(f, width);
    be16(f, depth);
    be16(f, 3);
    be32(f, 0); // color mode data
    be32(f, 0); // image resources

    QByteArray records, pixels;
    QImage merged(width, height, QImage::Format_RGB32);
    merged.fill(Qt::white);
    for (const TestLayer &l : layers) {
        const int w = l.rect.width(), h = l.rect.height();
        const int ids[4] = {-1, 0, 1, 2}, values[4] = {255, l.color.red(), l.color.green(), l.color.blue()};
        be32(records, l.rect.top());
        be32(records, l.rect.left());
        be32(records, l.rect.top() + h);
        be32(records, l.rect.left() + w);
        be16(records, 4);
        QByteArray channels[4];
        for (int k = 0; k < 4; ++k) {
            channels[k] = encode({plane(w, h, bytes, values[k])}, w, h, bytes, l.compression);
            be16(records, ids[k]);
            be32(records, channels[k].size());
            pixels += channels[k];
        }
        records += "8BIMnorm";
        records.append(char(255)).append('\0').append(char(l.hidden ? 2 : 0)).append('\0');
        QByteArray extra;
        be32(extra, 0); // mask
        be32(extra, 0); // blending ranges
        const QByteArray name = l.name.toLatin1();
        extra.append(char(name.size())).append(name);
        while ((extra.size() - 8) % 4) extra.append('\0');
        if (!l.unicodeName.isEmpty()) {
            extra += "8BIMluni";
            be32(extra, 4 + 2 * l.unicodeName.size());
            be32(extra, l.unicodeName.size());
            for (QChar c : l.unicodeName) be16(extra, c.unicode());
        }
        if (l.section) {
            extra += "8BIMlsct";
            be32(extra, 4);
            be32(extra, l.section);
        }
        be32(records, extra.size());
        records += extra;
        if (!l.section && !l.hidden && !blankMerged) // opaque layers, a visible group: drawn in turn
            for (int y = l.rect.top(); y <= l.rect.bottom(); ++y)
                for (int x = l.rect.left(); x <= l.rect.right(); ++x) merged.setPixelColor(x, y, l.color);
    }
    QByteArray info;
    be16(info, int(layers.size()));
    info += records + pixels;
    if (info.size() % 2) info.append('\0');
    QByteArray section;
    be32(section, info.size());
    section += info;
    be32(section, 0); // global layer mask
    be32(f, section.size());
    f += section;

    QVector<QByteArray> planes(3);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const QColor c = merged.pixelColor(x, y);
            const int v[3] = {c.red(), c.green(), c.blue()};
            for (int k = 0; k < 3; ++k) planes[k] += plane(1, 1, bytes, v[k]);
        }
    f += encode(planes, width, height, bytes, Rle);
    return f;
}

bool save(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

const QVector<TestLayer> kLayers = {
    {"Background", {}, QRect(0, 0, 32, 16), QColor(255, 0, 0), false, 0, Raw},
    {"Blu", "Blue", QRect(4, 4, 8, 8), QColor(0, 0, 255), false, 0, Rle}, // its Unicode name wins
    {"Green", {}, QRect(20, 2, 8, 8), QColor(0, 255, 0), true, 0, Zip},
    {"</Layer group>", {}, QRect(), QColor(), false, 3, Raw},
    {"Inside", {}, QRect(16, 8, 8, 8), QColor(255, 255, 0), false, 0, ZipPredicted},
    {"Grp", {}, QRect(), QColor(), false, 1, Raw},
};
constexpr int kBlue = 1, kGreen = 2, kInside = 4;

bool near(const QColor &c, int r, int g, int b, int a = 255)
{
    return std::abs(c.red() - r) <= 2 && std::abs(c.green() - g) <= 2 && std::abs(c.blue() - b) <= 2 && std::abs(c.alpha() - a) <= 2;
}
} // namespace

int runPsdLayerTests(Engine &e, const QString &tmp)
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::printf("FAIL psd: %s\n", why);
        } else {
            std::printf("ok    psd: %s\n", why);
        }
    };
    const QString path = tmp + "/layers.psd", blank = tmp + "/blank_merged.psd", deep = tmp + "/layers16.psd";
    const QByteArray file = writePsd(32, 16, 8, kLayers, false);
    check(save(path, file) && save(blank, writePsd(32, 16, 8, kLayers, true)) &&
              save(deep, writePsd(32, 16, 16, kLayers, false)), "write the Photoshop fixtures");

    // The layer list: names (Unicode first), group paths, visibility, places
    QString err;
    QSize canvas;
    const QVector<PsdLayerInfo> layers = psdLayers(path, &err, &canvas);
    check(err.isEmpty() && layers.size() == 6 && canvas == QSize(32, 16), "six layer records, canvas size");
    if (layers.size() == 6) {
        check(layers[kBlue].name == "Blue" && layers[kBlue].path == "Blue", "Unicode name preferred");
        check(layers[kBlue].rect == QRect(4, 4, 8, 8) && layers[0].rect == QRect(0, 0, 32, 16), "layer rects");
        check(!layers[kGreen].visible && layers[kBlue].visible, "hidden flag");
        check(layers[kInside].path == "Grp / Inside" && layers[kInside].name == "Inside", "group path");
        check(layers[3].group && layers[5].group && !layers[kInside].group, "group records marked");
        check(layers[kBlue].blend == "norm" && layers[kBlue].opacity == 255, "blend key and opacity");
    }

    // All layers: the merged image (RLE), or the layers composited when it is blank
    for (const QString &p : {path, blank}) {
        const QImage all = readPsd(p, -1, &err);
        check(!all.isNull() && all.size() == QSize(32, 16) && all.format() == QImage::Format_RGBA8888, "composite read");
        if (all.isNull()) continue;
        check(near(all.pixelColor(1, 1), 255, 0, 0), "composite: background red");
        check(near(all.pixelColor(6, 6), 0, 0, 255), "composite: blue layer");
        check(near(all.pixelColor(20, 12), 255, 255, 0), "composite: layer inside the group");
        check(near(all.pixelColor(24, 4), 255, 0, 0), "composite: hidden layer left out");
    }
    check(near(readImageFile(path).pixelColor(6, 6), 0, 0, 255), "readImageFile reads all the layers");

    // One layer alone, at its place, transparent elsewhere (RLE, ZIP, ZIP with prediction)
    const QImage blue = readPsd(path, kBlue, &err);
    check(!blue.isNull() && near(blue.pixelColor(6, 6), 0, 0, 255) && blue.pixelColor(0, 0).alpha() == 0,
          "RLE layer alone: blue at (6,6), transparent at (0,0)");
    const QImage green = readPsd(path, kGreen, &err);
    check(!green.isNull() && near(green.pixelColor(22, 4), 0, 255, 0) && green.pixelColor(1, 1).alpha() == 0,
          "ZIP layer alone (hidden layers can be chosen)");
    const QImage inside = readPsd(path, kInside, &err);
    check(!inside.isNull() && near(inside.pixelColor(23, 15), 255, 255, 0) && inside.pixelColor(15, 8).alpha() == 0,
          "ZIP layer with prediction alone");
    check(readPsd(path, 5, &err).isNull() && !err.isEmpty(), "a group is not a picture");
    check(readPsd(path, 9, &err).isNull() && !err.isEmpty(), "a layer that is not in the file");

    // 16 bits
    const QImage deepAll = readPsd(deep, -1, &err), deepBlue = readPsd(deep, kBlue, &err);
    check(deepAll.format() == QImage::Format_RGBA64 && near(deepAll.pixelColor(20, 12), 255, 255, 0), "16-bit composite");
    check(deepBlue.format() == QImage::Format_RGBA64 && near(deepBlue.pixelColor(6, 6), 0, 0, 255) &&
              deepBlue.pixelColor(0, 0).alpha() == 0, "16-bit layer alone");

    // Cut short anywhere: an error, never a crash; its merged image cut, the layers are composited
    const QString cut = tmp + "/cut.psd";
    for (int n = 0; n < file.size(); n += 5) {
        save(cut, file.left(n));
        psdLayers(cut, &err);
        readPsd(cut, -1, &err);
        readPsd(cut, kBlue, &err);
    }
    check(save(cut, file.left(40)), "write a cut file");
    err.clear();
    check(psdLayers(cut, &err).isEmpty() && !err.isEmpty(), "cut file: no layers, an error");
    err.clear();
    check(readPsd(cut, kBlue, &err).isNull() && !err.isEmpty(), "cut file: no picture, an error");
    save(cut, file.left(file.size() - 10));
    check(near(readPsd(cut, -1, &err).pixelColor(20, 12), 255, 255, 0), "merged image cut: layers composited");

    // A layer showing it: all its layers, then one of them
    e.newProject();
    int row = e.addLayer("PSD");
    const quint64 id = e.layerId(row);
    check(e.setLayerImage(row, path, &err), "load the PSD on a layer");
    e.renderFrame();
    QImage src = e.grabLayerSource(row);
    check(near(src.pixelColor(1, 1), 255, 0, 0) && near(src.pixelColor(6, 6), 0, 0, 255), "layer shows all the layers");
    check(!e.layerJson(row).value("source").toObject().contains("psd_layer"), "all layers: nothing saved for it");
    check(e.setLayerPsdLayer(row, kBlue, &err), "choose the Blue layer");
    e.renderFrame();
    src = e.grabLayerSource(row);
    check(near(src.pixelColor(6, 6), 0, 0, 255) && src.pixelColor(0, 0).alpha() < 3, "layer shows Blue alone");
    const QJsonObject source = e.layerJson(row).value("source").toObject();
    check(source.value("psd_layer").toInt(-1) == kBlue && source.value("psd_layer_name").toString() == "Blue",
          "choice in the layer's source");
    check(!e.setLayerPsdLayer(row, 5, &err) && !err.isEmpty() && e.layer(row)->psdLayer == kBlue,
          "a group cannot be chosen, the choice stays");

    // Undo puts the previous choice back
    {
        QUndoStack undo;
        const QJsonObject before = e.layerJson(row);
        check(e.setLayerPsdLayer(row, kInside, &err), "choose the layer inside the group");
        undo.push(new cmd::ReplaceLayer(&e, row, before, "Change PSD Layer"));
        undo.undo();
        check(e.layer(e.indexOfId(id))->psdLayer == kBlue, "Undo: Blue again");
        undo.redo();
        check(e.layer(e.indexOfId(id))->psdLayer == kInside, "Redo: the layer inside the group again");
        check(e.setLayerPsdLayer(e.indexOfId(id), kBlue, &err), "back to Blue");
    }

    // A snapshot holds the choice; recalled, it changes the picture
    Engine::Snapshot snapshot;
    snapshot.name = "Blue";
    snapshot.fade = 0;
    snapshot.layers = e.captureLayers(true);
    const int si = e.addSnapshot(snapshot);
    check(e.setLayerPsdLayer(e.indexOfId(id), -1, &err), "back to all layers");
    e.renderFrame();
    check(near(e.grabLayerSource(e.indexOfId(id)).pixelColor(1, 1), 255, 0, 0), "all layers shown again");
    e.recallSnapshot(si);
    for (int k = 0; k < 3; ++k) e.renderFrame();
    row = e.indexOfId(id);
    src = e.grabLayerSource(row);
    check(row >= 0 && e.layer(row)->psdLayer == kBlue && src.pixelColor(1, 1).alpha() < 3 &&
              near(src.pixelColor(6, 6), 0, 0, 255), "snapshot recall switches the PSD layer");

    // Saved and opened again with the project
    const QString project = tmp + "/psd.pinol";
    check(e.saveProject(project, {}, &err), "save the project");
    e.newProject();
    check(e.loadProject(project, nullptr, &err), "open the project");
    row = -1;
    for (int k = 0; k < e.layerCount(); ++k)
        if (e.layer(k)->name == "PSD") row = k;
    check(row >= 0 && e.layer(row)->psdLayer == kBlue && e.layer(row)->psdLayerName == "Blue", "choice restored");
    if (row >= 0) {
        e.renderFrame();
        src = e.grabLayerSource(row);
        check(near(src.pixelColor(6, 6), 0, 0, 255) && src.pixelColor(0, 0).alpha() < 3, "restored layer shows Blue alone");

        // Found again by its name when the file has changed; all layers, said so, when it is gone
        check(e.setLayerImage(row, path, &err, kGreen, "Blue") && e.layer(row)->psdLayer == kBlue &&
                  e.layer(row)->error.isEmpty(), "index moved: found by its name");
        check(e.setLayerImage(row, path, &err, kBlue, "Gone") && e.layer(row)->psdLayer == -1 &&
                  !e.layer(row)->error.isEmpty(), "name not found: all layers, with a warning");
    }
    e.newProject();
    return failures;
}
