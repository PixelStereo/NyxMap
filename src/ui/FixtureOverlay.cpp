#include "FixtureOverlay.h"

#include <QLineF>
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

quint64 hit(const FixturePatch &p, QSize comp, const ToWidget &toWidget, QPointF at)
{
    for (auto it = p.fixtures.rbegin(); it != p.fixtures.rend(); ++it) {
        const QPolygonF poly = outline(*it, comp, toWidget);
        if (poly.containsPoint(at, Qt::OddEvenFill)) return it->id;
        for (int k = 0; k < 4; ++k)
            if (distanceToSegment(at, poly[k], poly[(k + 1) % 4]) <= kCatch) return it->id;
    }
    return 0;
}

QString label(const Fixture &f)
{
    return QStringLiteral("%1 · %2.%3").arg(f.name.isEmpty() ? QStringLiteral("Fixture") : f.name).arg(f.universe)
        .arg(f.address, 3, 10, QLatin1Char('0'));
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

void paint(QPainter &painter, const FixturePatch &p, QSize comp, const ToWidget &toWidget, quint64 selected, bool grips)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() * 0.8);
    painter.setFont(font);
    for (const Fixture &f : p.fixtures) {
        const FixtureType *t = p.findType(f.type);
        const bool on = f.enabled && t;
        const QColor color = f.id == selected ? kSelected : on ? kFixture : kFixtureOff;
        const QPolygonF poly = outline(f, comp, toWidget);
        painter.setPen(QPen(color, f.id == selected ? 1.6 : 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(poly);
        if (t) {
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
        const QRectF box = poly.boundingRect();
        const QRectF text(box.left(), box.top() - 16, std::max(80.0, box.width() + 60), 14);
        painter.setPen(QColor(0, 0, 0, 200));
        painter.drawText(text.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter, label(f));
        painter.setPen(color);
        painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, label(f));
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
