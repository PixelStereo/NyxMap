#pragma once
// The Line generator, built in like Text or Countdown (MadMapper's Line): lines drawn as segments on the layer
// (polylines, open or closed, as many as wanted), then animated along them — a solid color or a gradient, a fill from
// their start to their end, dashes chasing along them, a strobe, a pulse — or a media (an image, another layer) laid
// along them, its width following the path. Each line or each segment can start later than the one before it (a
// cascade). The lines can carry pixel mapping fixtures, their pixels along the path (Fixtures.h).
// It is an ISF generator (Isf.h) whose path is a virtual one: Isf.cpp reads its shader from here, not from a file.
// Its look and its animation are the shader's inputs, so they are the layer's parameters (inspector, OSC, timelines,
// snapshots). Its lines are the layer's (Layer::lines), drawn in the Mapping view, saved with its source; the render
// thread hands them to the shader as a texture of segments (lineSegmentData).

#include <QJsonArray>
#include <QPointF>
#include <QString>
#include <vector>

// In the Media Bin under Generators, dragged onto a layer like any media; the layer's source path in a project
inline const QString kLineGeneratorPath = QStringLiteral("/Pinol/Generators/Line");
inline bool isLineGeneratorPath(const QString &p) { return QString(p).replace('\\', '/').endsWith(kLineGeneratorPath); }

// The ISF source of the shader of a built-in generator path; empty for any other path
QString builtInIsfSource(const QString &path);

// A line drawn on a Line layer: its points in the layer's own frame (0..1 over its picture, y down), joined in order,
// the last one to the first when closed
struct LinePath {
    std::vector<QPointF> points;
    bool closed = false;
    bool operator==(const LinePath &o) const { return points == o.points && closed == o.closed; }
    bool operator!=(const LinePath &o) const { return !(*this == o); }
    int segmentCount() const;
    QPointF segmentStart(int k) const { return points[size_t(k)]; }
    QPointF segmentEnd(int k) const { return points[size_t((k + 1) % int(points.size()))]; }
    // Its length in pixels of a picture of that size (the frame is not square)
    double length(double width, double height) const;
    // `n` points spread along it, the first and the last half a step from its ends (a closed one: all round it), in
    // its frame — where n pixels of a LED strip on it are
    std::vector<QPointF> spread(int n, double width, double height) const;
};
QJsonArray linePathsToJson(const std::vector<LinePath> &lines);
std::vector<LinePath> linePathsFromJson(const QJsonArray &a); // lines of fewer than two points dropped

// The segments of the lines as the shader reads them: 2 rows of `count` RGBA floats (count at least 1).
// Row 0: x0, y0, x1, y1 (the frame, y down). Row 1: where it starts and ends along its line (0..1 of its length), its
// line's index, its own index in its line. Lengths measured in pixels of a picture of that size
std::vector<float> lineSegmentData(const std::vector<LinePath> &lines, double width, double height, int *count);
