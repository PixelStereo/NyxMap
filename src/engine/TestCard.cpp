#include "TestCard.h"

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {

// Where the moving parts are, in pixels from the top left (the still card leaves room for them)
struct Geometry {
    double w, h, unit;      // unit: the short side
    QPointF center;
    double radius;
    QRectF bars, steps, band;
    double corner;
};

Geometry geometry(QSize size)
{
    Geometry g;
    g.w = std::max(1, size.width());
    g.h = std::max(1, size.height());
    g.unit = std::min(g.w, g.h);
    g.center = QPointF(g.w / 2, g.h / 2);
    g.radius = g.unit * 0.40;
    g.bars = QRectF(g.w * 0.16, g.h * 0.66, g.w * 0.68, g.h * 0.08);
    g.steps = QRectF(g.bars.left(), g.bars.bottom(), g.bars.width(), g.h * 0.05);
    g.band = QRectF(g.bars.left(), g.steps.bottom() + g.h * 0.015, g.bars.width(), g.h * 0.035);
    g.corner = g.unit * 0.13;
    return g;
}

} // namespace

QImage TestCard::still(const QString &name, QSize size, const QString &outputName)
{
    const Geometry g = geometry(size);
    QImage img(int(g.w), int(g.h), QImage::Format_RGBA8888);
    img.fill(QColor(28, 28, 30));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    // Grid of squares through the center, the center cross a little brighter, the diagonals
    const double step = std::max(8.0, std::round(g.h / 10));
    p.setPen(QPen(QColor(85, 85, 90), std::max(1.0, g.unit / 1080)));
    for (double x = std::fmod(g.center.x(), step); x < g.w; x += step) p.drawLine(QPointF(x, 0), QPointF(x, g.h));
    for (double y = std::fmod(g.center.y(), step); y < g.h; y += step) p.drawLine(QPointF(0, y), QPointF(g.w, y));
    p.setPen(QPen(QColor(150, 150, 155), std::max(1.0, g.unit / 720)));
    p.drawLine(QPointF(g.center.x(), 0), QPointF(g.center.x(), g.h));
    p.drawLine(QPointF(0, g.center.y()), QPointF(g.w, g.center.y()));
    p.setPen(QPen(QColor(110, 110, 115), std::max(1.0, g.unit / 1080)));
    p.drawLine(QPointF(0, 0), QPointF(g.w, g.h));
    p.drawLine(QPointF(g.w, 0), QPointF(0, g.h));

    // The circle: round only if the picture's aspect is right
    p.setPen(QPen(Qt::white, std::max(2.0, g.unit / 360)));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(g.center, g.radius, g.radius);

    // Color bars and grey steps
    const QColor bars[8] = {Qt::white, QColor(255, 255, 0), QColor(0, 255, 255), QColor(0, 255, 0),
                            QColor(255, 0, 255), QColor(255, 0, 0), QColor(0, 0, 255), Qt::black};
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 8; ++i)
        p.fillRect(QRectF(g.bars.left() + g.bars.width() * i / 8, g.bars.top(), g.bars.width() / 8 + 1, g.bars.height()), bars[i]);
    for (int i = 0; i <= 10; ++i) {
        const int v = int(std::lround(255.0 * i / 10));
        p.fillRect(QRectF(g.steps.left() + g.steps.width() * i / 11, g.steps.top(), g.steps.width() / 11 + 1, g.steps.height()),
                   QColor(v, v, v));
    }
    p.fillRect(g.band, QColor(55, 55, 60)); // where the block moves

    // The four corners: numbered and colored, an arrow to the corner
    struct Corner {
        QPointF at;   // its corner
        QPointF dir;  // towards the inside
        QColor color;
        QString label;
    };
    const Corner corners[4] = {{{0, 0}, {1, 1}, QColor(230, 40, 40), QStringLiteral("1")},
                               {{g.w, 0}, {-1, 1}, QColor(40, 200, 70), QStringLiteral("2")},
                               {{g.w, g.h}, {-1, -1}, QColor(50, 110, 255), QStringLiteral("3")},
                               {{0, g.h}, {1, -1}, QColor(240, 210, 30), QStringLiteral("4")}};
    QFont cf(QStringLiteral("Helvetica"));
    cf.setBold(true);
    cf.setPixelSize(std::max(8, int(g.corner * 0.55)));
    p.setFont(cf);
    for (const Corner &c : corners) {
        QPainterPath tri; // a triangle filling the corner
        tri.moveTo(c.at);
        tri.lineTo(c.at + QPointF(c.dir.x() * g.corner * 1.6, 0));
        tri.lineTo(c.at + QPointF(0, c.dir.y() * g.corner * 1.6));
        tri.closeSubpath();
        p.fillPath(tri, c.color);
        const QPointF labelCenter = c.at + QPointF(c.dir.x() * g.corner * 0.5, c.dir.y() * g.corner * 0.5);
        p.setPen(c.color == QColor(240, 210, 30) ? Qt::black : Qt::white);
        p.drawText(QRectF(labelCenter - QPointF(g.corner, g.corner), QSizeF(g.corner * 2, g.corner * 2)), Qt::AlignCenter, c.label);
    }

    // A frame on the outermost pixels: whatever is cropped shows
    p.setPen(QPen(Qt::white, 1));
    p.setRenderHint(QPainter::Antialiasing, false);
    p.drawRect(QRectF(0, 0, g.w - 1, g.h - 1));
    p.setRenderHint(QPainter::Antialiasing);

    // Its name and size, on a dark plate in the middle
    QFont nf(QStringLiteral("Helvetica"));
    nf.setBold(true);
    nf.setPixelSize(std::max(10, int(g.unit * 0.11)));
    QFont sf(QStringLiteral("Helvetica"));
    sf.setPixelSize(std::max(8, int(g.unit * 0.045)));
    const QString sizeText = QStringLiteral("%1 × %2").arg(size.width()).arg(size.height());
    const QFontMetricsF nm(nf), sm(sf);
    const double outputHeight = outputName.isEmpty() ? 0 : sm.height();
    const double plateW = std::min(g.w * 0.9, std::max({nm.horizontalAdvance(name), sm.horizontalAdvance(sizeText),
                                                     sm.horizontalAdvance(outputName)}) + g.unit * 0.08);
    const double plateH = nm.height() + outputHeight + sm.height() + g.unit * 0.04;
    const QRectF plate(g.center.x() - plateW / 2, g.center.y() - plateH / 2, plateW, plateH);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 200));
    p.drawRoundedRect(plate, g.unit * 0.015, g.unit * 0.015);
    p.setPen(Qt::white);
    p.setFont(nf);
    p.drawText(QRectF(plate.left(), plate.top() + g.unit * 0.015, plate.width(), nm.height()), Qt::AlignCenter,
               nm.elidedText(name, Qt::ElideRight, plate.width() - g.unit * 0.04));
    p.setPen(QColor(200, 200, 205));
    p.setFont(sf);
    const double subtitleTop = plate.top() + g.unit * 0.015 + nm.height();
    if (!outputName.isEmpty()) {
        p.drawText(QRectF(plate.left(), subtitleTop, plate.width(), outputHeight), Qt::AlignCenter,
                   sm.elidedText(outputName, Qt::ElideRight, plate.width() - g.unit * 0.04));
    }
    p.drawText(QRectF(plate.left(), subtitleTop + outputHeight, plate.width(), sm.height()), Qt::AlignCenter, sizeText);
    p.end();
    return img;
}

TestCard::Motion TestCard::motion(QSize size)
{
    const Geometry g = geometry(size);
    Motion m;
    m.size[0] = float(g.w);
    m.size[1] = float(g.h);
    // In pixels from the bottom left, as the shader reads them
    m.circle[0] = float(g.center.x());
    m.circle[1] = float(g.h - g.center.y());
    m.circle[2] = float(g.radius);
    m.band[0] = float(g.band.left());
    m.band[1] = float(g.band.right());
    m.band[2] = float(g.h - g.band.bottom());
    m.band[3] = float(g.h - g.band.top());
    return m;
}
