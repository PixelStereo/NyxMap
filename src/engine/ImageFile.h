#pragma once
#include <QImage>
#include <QSize>
#include <QString>
#include <vector>

// A picture file: through Qt's image plugins, else decoded by FFmpeg (TIFF where Qt's plugin is not shipped,
// OpenEXR…). Null if neither can read it. Any thread.
// A Photoshop file is read here (Psd.h): all its layers (psdLayer -1; FFmpeg's reading of its merged image when it
// cannot be), or one of them alone on the canvas (null with err when it cannot be read).
// An SVG drawing (when Qt SVG is there) is rendered on a transparent background at its own size, scaled up so that its
// longer side is at least `svgLongSide` (a layer: the composition's), at most kMaxSvgSide.
QImage readImageFile(const QString &path, int psdLayer = -1, QString *err = nullptr, int svgLongSide = 0);
constexpr int kMaxSvgSide = 8192;
bool isSvgFile(const QString &path);
// A high dynamic range picture (.hdr, .exr): its pixels as linear floats, not clipped (a sun stays far above 1), RGBA,
// its first row at the bottom, scaled down to maxWidth at most. False when it is not one or cannot be read
bool isHdrImageFile(const QString &path);
bool readHdrPixels(const QString &path, std::vector<float> *rgba, QSize *size, int maxWidth = 2048);
// The size an SVG drawing is rendered at (invalid: not one, or unreadable)
QSize svgRenderSize(const QString &path, int svgLongSide);
