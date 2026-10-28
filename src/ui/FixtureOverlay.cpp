#include "FixtureOverlay.h"

#include <QLineF>
#include <optional>
#include <algorithm>
#include <cmath>

namespace {
const QColor kFixture(90, 220, 170, 230);
const QColor kFixtureOff(150, 150, 150, 150);
const QColor kSelected(255, 150, 40, 240);
constexpr double kCatch = 6;     // widget pixels around a fixture that still pick it
constexpr double kRotateGap = 22; // widget pixels between the top edge and the rotation grip

double distanceToSegment(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    return QLineF(p, a + t * ab).length();
}
} // namespace

namespace fixtureview {

QPolygonF outline(const Fixture &f, QSize comp, const ToWidget &toWidget)
{
    QPolygonF poly;
    for (const QPointF &c : fixtureCorners(f, comp)) poly << toWidget(c);
    return poly;
}

// A strip's path in the widget, closed or not
static QPolygonF pathOf(const Fixture &f, const ToWidget &toWidget)
{
    QPolygonF poly;
    for (const QPointF &q : f.path) poly << toWidget(q);
    if (f.pathClosed && poly.size() > 2) poly << poly.front();
    return poly;
}

quint64 hit(const FixturePatch &p, QSize comp, const ToWidget &toWidget, QPointF at)
{
    for (auto it = p.fixtures.rbegin(); it != p.fixtures.rend(); ++it) {
        if (it->onPath()) { // a strip: along its path
            const QPolygonF poly = pathOf(*it, toWidget);
            for (int k = 0; k + 1 < poly.size(); ++k)
                if (distanceToSegment(at, poly[k], poly[k + 1]) <= kCatch) return it->id;
            continue;
        }
        const QPolygonF poly = outline(*it, comp, toWidget);
        if (poly.containsPoint(at, Qt::OddEvenFill)) return it->id;
        for (int k = 0; k < 4; ++k)
            if (distanceToSegment(at, poly[k], poly[(k + 1) % 4]) <= kCatch) return it->id;
    }
    return 0;
}

QString address(const FixturePatch &p, const Fixture &f)
{
    const std::optional<FixtureType> t = p.resolvedType(f);
    if (!t) return QStringLiteral("%1 · %2").arg(f.universe).arg(f.address);
    QStringList spans;
    for (const DmxSpan &s : fixtureFootprint(*t, f.universe, f.address))
        spans << (s.first == s.last ? QStringLiteral("%1 · %2").arg(s.universe).arg(s.first)
                                    : QStringLiteral("%1 · %2-%3").arg(s.universe).arg(s.first).arg(s.last));
    return spans.join(QStringLiteral(", "));
}

QString label(const FixturePatch &p, const Fixture &f)
{
    return QStringLiteral("%1 · %2").arg(f.name.isEmpty() ? QStringLiteral("Fixture") : f.name, address(p, f));
}

QPointF resizeGrip(const Fixture &f, QSize comp, const ToWidget &toWidget) { return toWidget(fixtureCorners(f, comp)[2]); }

QPointF rotateGrip(const Fixture &f, QSize comp, const ToWidget &toWidget)
{
    const auto c = fixtureCorners(f, comp);
    const QPointF top = (toWidget(c[0]) + toWidget(c[1])) / 2, bottom = (toWidget(c[3]) + toWidget(c[2])) / 2;
    QPointF up = top - bottom;
    const double len = std::hypot(up.x(), up.y());
    up = len > 1e-6 ? up / len : QPointF(0, -1);
    return top + up * kRotateGap;
}

void paint(QPainter &painter, const FixturePatch &p, QSize comp, const ToWidget &toWidget, const Looks &looks,
           quint64 selected, bool grips)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() * 0.8);
    painter.setFont(font);
    for (const Fixture &f : p.fixtures) {
        const std::optional<FixtureType> t = p.resolvedType(f);
        const bool on = f.enabled && t;
        const QColor color = f.id == selected ? kSelected : on ? kFixture : kFixtureOff;
        const QPolygonF poly = outline(f, comp, toWidget);
        const auto look = looks.find(f.id);
        if (f.onPath()) { // a strip: its path, its pixels dots along it (filled with what they give out)
            painter.setPen(QPen(color, f.id == selected ? 1.6 : 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(pathOf(f, toWidget));
            if (t) {
                const std::vector<QPointF> px = fixturePixelPoints(f, *t, comp);
                const bool looked = look != looks.end() && look->second.size() == px.size();
                double step = 8;
                if (px.size() > 1) step = QLineF(toWidget(px[0]), toWidget(px[1])).length();
                const double r = std::clamp(step * 0.35, 1.0, 4.0);
                painter.setPen(looked ? QPen(QColor(0, 0, 0, 140), 0.8) : Qt::NoPen);
                for (size_t k = 0; k < px.size(); ++k) {
                    if (looked) {
                        const auto &rgb = look->second[k];
                        painter.setBrush(QColor::fromRgbF(std::clamp(rgb[0], 0.0f, 1.0f), std::clamp(rgb[1], 0.0f, 1.0f),
                                                          std::clamp(rgb[2], 0.0f, 1.0f)));
                    } else {
                        painter.setBrush(color);
                    }
                    painter.drawEllipse(toWidget(px[k]), r, r);
                }
                if (!px.empty()) { // where the DMX starts
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(color, 1.0));
                    painter.drawEllipse(toWidget(px.front()), r + 2.5, r + 2.5);
                }
            }
            const QPointF at = toWidget(f.path.front());
            const QRectF text(at.x() + 8, at.y() - 18, 240, 14);
            const QString name = label(p, f) + (f.lineLayer ? QStringLiteral("  ⟿") : QString()) +
                                 (f.locked ? QStringLiteral("  🔒") : QString()) + (f.solo ? QStringLiteral("  S") : QString());
            painter.setPen(QColor(0, 0, 0, 200));
            painter.drawText(text.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter, name);
            painter.setPen(color);
            painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, name);
            continue;
        }
        if (t && look != looks.end() && int(look->second.size()) == t->pixelCount()) {
            // Each pixel's cell, filled with what it gives out (corners through toWidget: a viewport may bend them)
            const auto c = fixtureCorners(f, comp);
            auto at = [&](double u, double v) { return toWidget(c[0] + u * (c[1] - c[0]) + v * (c[3] - c[0])); };
            painter.setPen(Qt::NoPen);
            for (int k = 0; k < t->pixelCount(); ++k) {
                const QPoint cell = t->pixelCell(k);
                const double u0 = double(cell.x()) / t->cols, u1 = double(cell.x() + 1) / t->cols;
                const double v0 = double(cell.y()) / t->rows, v1 = double(cell.y() + 1) / t->rows;
                const auto &rgb = look->second[size_t(k)];
                painter.setBrush(QColor::fromRgbF(std::clamp(rgb[0], 0.0f, 1.0f), std::clamp(rgb[1], 0.0f, 1.0f),
                                                  std::clamp(rgb[2], 0.0f, 1.0f)));
                painter.drawPolygon(QPolygonF{at(u0, v0), at(u1, v0), at(u1, v1), at(u0, v1)});
            }
            if (t->pixelCount() > 1 && poly.boundingRect().width() / t->cols > 6) { // the cells' borders, when visible
                painter.setPen(QPen(QColor(0, 0, 0, 120), 1.0));
                for (int x = 1; x < t->cols; ++x) painter.drawLine(at(double(x) / t->cols, 0), at(double(x) / t->cols, 1));
                for (int y = 1; y < t->rows; ++y) painter.drawLine(at(0, double(y) / t->rows), at(1, double(y) / t->rows));
            }
        }
        painter.setPen(QPen(color, f.id == selected ? 1.6 : 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(poly);
        if (t && look == looks.end()) {
            // Its pixels: dots as large as their cells allow (a few pixels at most)
            const std::vector<QPointF> px = fixturePixelPoints(f, *t, comp);
            const double cellW = QLineF(poly[0], poly[1]).length() / std::max(1, t->cols);
            const double cellH = QLineF(poly[0], poly[3]).length() / std::max(1, t->rows);
            const double r = std::clamp(std::min(cellW, cellH) * 0.3, 0.8, 3.5);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            for (const QPointF &q : px) painter.drawEllipse(toWidget(q), r, r);
            // The first pixel (where the DMX starts) ringed
            if (!px.empty()) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(color, 1.0));
                painter.drawEllipse(toWidget(px.front()), r + 2.5, r + 2.5);
            }
        }
        if (t && look != looks.end() && t->pixelCount() > 0) {
            // Where the DMX starts: the first pixel ringed
            const QPointF first = toWidget(fixturePixelPoints(f, *t, comp).front());
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(color, 1.0));
            painter.drawEllipse(first, 3.5, 3.5);
        }
        const QRectF box = poly.boundingRect();
        const QRectF text(box.left(), box.top() - 16, std::max(80.0, box.width() + 60), 14);
        const QString name = label(p, f) + (f.locked ? QStringLiteral("  🔒") : QString()) + (f.solo ? QStringLiteral("  S") : QString());
        painter.setPen(QColor(0, 0, 0, 200));
        painter.drawText(text.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter, name);
        painter.setPen(color);
        painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, name);
        if (grips && f.id == selected) {
            const QPointF rz = resizeGrip(f, comp, toWidget), rt = rotateGrip(f, comp, toWidget);
            const QPointF topMid = (poly[0] + poly[1]) / 2;
            painter.setPen(QPen(kSelected, 1.0));
            painter.drawLine(topMid, rt);
            painter.setBrush(kSelected);
            painter.drawRect(QRectF(rz.x() - 4, rz.y() - 4, 8, 8));
            painter.drawEllipse(rt, 4.5, 4.5);
        }
    }
    painter.restore();
}

} // namespace fixtureview
