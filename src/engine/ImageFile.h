#pragma once
#include <QImage>
#include <QString>

// A picture file: through Qt's image plugins, else decoded by FFmpeg (TIFF where Qt's plugin is not shipped,
// OpenEXR…). Null if neither can read it. Any thread.
// A Photoshop file is read here (Psd.h): all its layers (psdLayer -1; FFmpeg's reading of its merged image when it
// cannot be), or one of them alone on the canvas (null with err when it cannot be read).
QImage readImageFile(const QString &path, int psdLayer = -1, QString *err = nullptr);
