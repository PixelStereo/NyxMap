#pragma once
#include <QImage>
#include <QString>

// A picture file: through Qt's image plugins, else decoded by FFmpeg (TIFF where Qt's plugin is not shipped,
// OpenEXR, Photoshop…). Null if neither can read it. Any thread.
QImage readImageFile(const QString &path);
