#pragma once
// Small image helpers shared by the engine: the rows' order between images (top row first) and textures (bottom row
// first), and the format a picture is kept in.

#include <QImage>
#include <QPixelFormat>
#include <algorithm>

// Upside down: images (top row first) to and from textures (bottom row first). QImage::mirrored is deprecated
// since Qt 6.13, flipped exists since Qt 6.9.
inline QImage flippedVertically(const QImage &img)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return img.flipped(Qt::Vertical);
#else
    return img.mirrored(false, true);
#endif
}

// Preserve integer source channels wider than 8 bits. RGBA64 is native-endian straight RGBA. Float/HDR sources retain the
// existing SDR conversion policy; higher integer precision is not color management.
inline QImage::Format imageUploadFormat(const QImage &image)
{
    const QPixelFormat p = image.pixelFormat();
    const bool high = p.typeInterpretation() != QPixelFormat::FloatingPoint &&
                     std::max({p.redSize(), p.greenSize(), p.blueSize(), p.alphaSize()}) > 8;
    return high ? QImage::Format_RGBA64 : QImage::Format_RGBA8888;
}

inline QImage prepareImageUpload(const QImage &image)
{
    return flippedVertically(image.convertToFormat(imageUploadFormat(image)));
}
