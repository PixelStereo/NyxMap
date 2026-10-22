#pragma once
#include <QImage>
#include <QSize>
#include <QString>

// A picture file: through Qt's image plugins, else decoded by FFmpeg (TIFF where Qt's plugin is not shipped,
// OpenEXR…). Null if neither can read it. Any thread.
// A Photoshop file is read here (Psd.h): all its layers (psdLayer -1; FFmpeg's reading of its merged image when it
// cannot be), or one of them alone on the canvas (null with err when it cannot be read).
// An SVG drawing (when Qt SVG is there) is rendered on a transparent background at its own size, scaled up so that its
// longer side is at least `svgLongSide` (a layer: the composition's), at most kMaxSvgSide.
QImage readImageFile(const QString &path, int psdLayer = -1, QString *err = nullptr, int svgLongSide = 0);
constexpr int kMaxSvgSide = 8192;
bool isSvgFile(const QString &path);
// The size an SVG drawing is rendered at (invalid: not one, or unreadable)
QSize svgRenderSize(const QString &path, int svgLongSide);
