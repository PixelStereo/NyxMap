#pragma once
#include <QByteArray>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

// Photoshop files (.psd, and .psb, the large document format), read as Adobe's "Photoshop File Formats
// Specification" describes them: RGB and grayscale, 8 and 16 bits per channel; raw, RLE (PackBits) and ZIP
// channel data. Any thread.

// One record of the file's layer list. Its index in the list (bottom first, as the file stores them) is how a layer
// is chosen; group records (a group's own and the end of one) are in the list too, never a picture to choose.
struct PsdLayerInfo {
    QString name;          // its Unicode name when the file has one
    QString path;          // with the groups it is in: "Group / Sub / Layer"
    QRect rect;            // where its pixels are on the canvas
    bool visible = true;   // its own eye (not its groups')
    int opacity = 255;     // 0..255
    bool group = false;    // a group's record, or the end of one
    QByteArray blend;      // its blend mode key ("norm", "mul "…)
};

bool isPsdFile(const QString &path); // by its extension
// The layers of the file, in its order. Empty with err for a file that cannot be read, or whose layers cannot
// be shown (a color mode other than RGB or grayscale, 32 bits); empty without err for a flat file. The canvas'
// size in `size`, when asked.
QVector<PsdLayerInfo> psdLayers(const QString &file, QString *err, QSize *size = nullptr);
// The picture at the canvas' size: layer -1 is all the layers (the file's merged image when it has a usable one,
// else its visible layers composited here, normal blending — see the .cpp), a layer index that layer alone at its
// place, transparent elsewhere. RGBA8888 for 8-bit files, RGBA64 for 16-bit ones. Null with err when it cannot.
QImage readPsd(const QString &file, int layer, QString *err);
