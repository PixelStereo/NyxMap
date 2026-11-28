#include <pxs/Theme.h>
#include "Widgets.h"
#include "Mapping.h"
#include "ColorPicker.h"
#include <QAbstractItemView>
#include <QDateTime>
#include <QMenu>
#include <QActionGroup>
#include <QSpinBox>
#include <QPointer>

#include <QCheckBox>

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QScrollBar>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleOption>
#include <QRegularExpression>
#include <QPixmap>
#include <QWheelEvent>
#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QSignalBlocker>
#include <QSlider>
#include <QCoreApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMap>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstdlib>


// ---------------------------------------------------------------------------
// Accent color
// ---------------------------------------------------------------------------

namespace theme {
namespace {
QColor g_accent;
bool g_loaded = false;
} // namespace

QColor defaultAccent() { return QColor(214, 214, 219); }

QColor accent()
{
    if (!g_loaded) {
        const QString v = QSettings().value(QStringLiteral("ui/accent")).toString();
        const QColor c(v);
        g_accent = c.isValid() ? c : defaultAccent();
        g_loaded = true;
    }
    return g_accent;
}

void setAccent(const QColor &c)
{
    g_accent = c.isValid() ? c : defaultAccent();
    g_loaded = true;
    QSettings().setValue(QStringLiteral("ui/accent"), g_accent.name());
    emit notifier()->changed();
}

// Black or white, whichever stands out on the accent (sRGB luminance)
QColor onAccent()
{
    const QColor a = accent();
    const double l = 0.2126 * a.redF() + 0.7152 * a.greenF() + 0.0722 * a.blueF();
    return l > 0.55 ? QColor(26, 26, 29) : QColor(240, 240, 243);
}

QString css(int alpha)
{
    const QColor a = accent();
    if (alpha >= 255) return a.name();
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(a.red()).arg(a.green()).arg(a.blue()).arg(alpha);
}

Notifier *notifier()
{
    static Notifier n;
    return &n;
}

} // namespace theme

QColor theme::cueColor(CueKind kind)
{
    switch (kind) {
    case CueKind::Snapshot: return QColor(172, 190, 207);
    case CueKind::Preset: return QColor(112, 142, 169);
    case CueKind::Timeline: return QColor(102, 178, 173);
    case CueKind::Group: return QColor(184, 121, 101);
    case CueKind::Sequence: return QColor(139, 144, 153);
    }
    return {};
}

namespace autoselect {
Notifier *notifier()
{
    static Notifier n;
    return &n;
}
bool enabled() { return QSettings().value(QStringLiteral("ui/autoSelect"), true).toBool(); }
void setEnabled(bool on)
{
    if (enabled() == on) return;
    QSettings().setValue(QStringLiteral("ui/autoSelect"), on);
    emit notifier()->changed();
}
QIcon icon()
{
    QIcon icon;
    for (int on = 0; on < 2; ++on) {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        // A pointer arrow over a small frame: the accent when on, grey when off
        const QColor c = on ? theme::accent() : QColor(118, 118, 124);
        p.setPen(QPen(c, 2.0, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(4, 4, 17, 13));
        QPainterPath arrow;
        arrow.moveTo(13, 10);
        arrow.lineTo(13, 29);
        arrow.lineTo(17.5, 24.5);
        arrow.lineTo(21, 31);
        arrow.lineTo(24, 29.5);
        arrow.lineTo(20.5, 23);
        arrow.lineTo(27, 23);
        arrow.closeSubpath();
        p.setPen(QPen(QColor(20, 20, 22), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(on ? QColor(235, 235, 238) : QColor(150, 150, 156));
        p.drawPath(arrow);
        icon.addPixmap(pm, QIcon::Normal, on ? QIcon::On : QIcon::Off);
    }
    return icon;
}
} // namespace autoselect

namespace magnet {
static bool g_enabled = true, g_enabledLoaded = false;
static int g_distance = -1;

Notifier *notifier()
{
    static Notifier n;
    return &n;
}
bool enabledAtStart() { return QSettings().value(QStringLiteral("ui/magnetism"), true).toBool(); }
void setEnabledAtStart(bool on) { QSettings().setValue(QStringLiteral("ui/magnetism"), on); }
bool enabled()
{
    if (!g_enabledLoaded) {
        g_enabled = enabledAtStart();
        g_enabledLoaded = true;
        SliderField::setSnappingEnabled(g_enabled); // the number fields live in pxs
    }
    return g_enabled;
}
void setEnabled(bool on)
{
    g_enabledLoaded = true;
    if (g_enabled == on) return;
    g_enabled = on;
    SliderField::setSnappingEnabled(on);
    emit notifier()->changed();
}
int distance()
{
    if (g_distance < 0) g_distance = std::clamp(QSettings().value(QStringLiteral("ui/magnetDistance"), 8).toInt(), 2, 40);
    return g_distance;
}
void setDistance(int px)
{
    g_distance = std::clamp(px, 2, 40);
    QSettings().setValue(QStringLiteral("ui/magnetDistance"), g_distance);
    emit notifier()->changed();
}
QIcon icon()
{
    QIcon icon;
    for (int on = 0; on < 2; ++on) {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        // A horseshoe magnet: red when on, grey when off, its poles silver
        const QColor body = on ? QColor(225, 62, 56) : QColor(118, 118, 124), poles(215, 215, 220);
        QPainterPath shoe;
        shoe.moveTo(8, 9);
        shoe.lineTo(8, 16);
        shoe.arcTo(QRectF(8, 8.5, 16, 17), 180, 180);
        shoe.lineTo(24, 9);
        p.setPen(QPen(body, 6.5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        p.drawPath(shoe);
        p.setPen(QPen(poles, 6.5, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(8, 3), QPointF(8, 9));
        p.drawLine(QPointF(24, 3), QPointF(24, 9));
        icon.addPixmap(pm, QIcon::Normal, on ? QIcon::On : QIcon::Off);
    }
    return icon;
}
} // namespace magnet

namespace theme {
// Dark interface, with the accent wherever a selection or an active control is shown
namespace {
// A selected row of a list is tinted, not filled: its content stays readable
class SoftSelection : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::Polish)
            if (auto *view = qobject_cast<QAbstractItemView *>(o)) {
                QPalette p = view->palette();
                const QColor base = p.color(QPalette::Base), a = accent();
                const double k = 0.30;
                const QColor soft(int(base.red() + (a.red() - base.red()) * k), int(base.green() + (a.green() - base.green()) * k),
                                  int(base.blue() + (a.blue() - base.blue()) * k));
                p.setColor(QPalette::Highlight, soft);
                p.setColor(QPalette::HighlightedText, QColor(240, 240, 242));
                view->setPalette(p);
            }
        return false;
    }
};
} // namespace

// The graphite look shared by every Pixel Stereo application (pxs::theme); the accent stays the one the user chose
static void applyLook(QApplication &app)
{
    pxs::Palette palette = pxs::graphite();
    palette.link = accent();
    pxs::apply(app, palette,
               QStringLiteral("QGroupBox { font-weight: bold; border: 1px solid @border; border-radius: 4px; "
                                           "margin-top: 10px; padding-top: 8px; } "
                                           "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; "
                                           "color: @focus; }"
                                           "QSplitter::handle { background:@border; margin:1px; }"
                                           "QSplitter::handle:horizontal { width:7px; }"
                                           "QSplitter::handle:vertical { height:7px; }"
                                           "QSplitter::handle:hover { background:@focus; }"
                                           "QHeaderView::section { background:@midlight; color:@text;"
                                           " border:0; border-right:1px solid @light; border-bottom:1px solid @border; padding:4px 6px; }"
                                           "QMenu::separator { height:1px; background:@border; margin:5px 8px; }"
                                           // A button that is on shows it: the accent, not Fusion's barely darker shade
                                           "QPushButton:checked, QToolButton:checked { background:@focus; color:%1; "
                                           "border:1px solid @focus; border-radius:3px; }")
                   .arg(onAccent().name()));
    // Selections follow the accent
    QPalette p = app.palette();
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, onAccent());
    app.setPalette(p);
}

void applyToApplication(QApplication &app)
{
    app.installEventFilter(new SoftSelection(&app));
    applyLook(app);
    // A new accent shows at once: the focus, the selection, the checked buttons and the tab underline
    QObject::connect(notifier(), &Notifier::changed, &app, [&app] { applyLook(app); });
}
} // namespace theme

// ---------------------------------------------------------------------------
// ResetLabel
// ---------------------------------------------------------------------------

ResetLabel::ResetLabel(const QString &text, std::function<void()> reset, QWidget *parent)
    : QLabel(text, parent), m_reset(std::move(reset))
{
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Click to reset \"%1\" to its default value").arg(text));
    setProperty("resetLabel", true);
}

void ResetLabel::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && isEnabled() && m_reset) {
        m_reset();
        e->accept();
        return;
    }
    QLabel::mousePressEvent(e);
}

void ResetLabel::enterEvent(QEnterEvent *e)
{
    if (isEnabled()) setStyleSheet(QStringLiteral("color:%1; text-decoration:underline;").arg(theme::css()));
    QLabel::enterEvent(e);
}

void ResetLabel::leaveEvent(QEvent *e)
{
    setStyleSheet(QString());
    QLabel::leaveEvent(e);
}

// ---------------------------------------------------------------------------
// XyPad
// ---------------------------------------------------------------------------

XyPad::XyPad(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(110);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    setCursor(Qt::CrossCursor);
    setToolTip(QStringLiteral("Drag: X across, Y upward · double-click: back to the default"));
}

void XyPad::setRange(double lo, double hi)
{
    m_lo = lo;
    m_hi = hi > lo ? hi : lo + 1;
    update();
}

void XyPad::setValue(double x, double y)
{
    if (m_dragging) return;
    m_x = std::clamp(x, m_lo, m_hi);
    m_y = std::clamp(y, m_lo, m_hi);
    update();
}

QRectF XyPad::area() const { return QRectF(rect()).adjusted(4, 4, -4, -4); }

void XyPad::drag(QPointF pos)
{
    const QRectF a = area();
    const double nx = std::clamp((pos.x() - a.left()) / std::max(1.0, a.width()), 0.0, 1.0);
    const double ny = std::clamp(1.0 - (pos.y() - a.top()) / std::max(1.0, a.height()), 0.0, 1.0);
    const double x = m_lo + nx * (m_hi - m_lo), y = m_lo + ny * (m_hi - m_lo);
    if (x == m_x && y == m_y) return;
    m_x = x;
    m_y = y;
    update();
    emit valueEdited(m_x, m_y);
}

void XyPad::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    m_dragging = true;
    drag(e->position());
}

void XyPad::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) drag(e->position());
}

void XyPad::mouseReleaseEvent(QMouseEvent *) { m_dragging = false; }

void XyPad::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    m_x = m_y = std::clamp(m_default, m_lo, m_hi);
    update();
    emit valueEdited(m_x, m_y);
}

void XyPad::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF a = area();
    p.setPen(QPen(QColor(70, 70, 78), 1));
    p.setBrush(QColor(30, 30, 34));
    p.drawRoundedRect(a, 5, 5);
    p.setPen(QPen(QColor(55, 55, 62), 1));
    p.drawLine(QPointF(a.center().x(), a.top()), QPointF(a.center().x(), a.bottom()));
    p.drawLine(QPointF(a.left(), a.center().y()), QPointF(a.right(), a.center().y()));
    const double nx = (m_x - m_lo) / (m_hi - m_lo), ny = (m_y - m_lo) / (m_hi - m_lo);
    const QPointF dot(a.left() + nx * a.width(), a.bottom() - ny * a.height());
    p.setPen(QPen(theme::accent(), 1));
    p.drawLine(QPointF(dot.x(), a.top()), QPointF(dot.x(), a.bottom()));
    p.drawLine(QPointF(a.left(), dot.y()), QPointF(a.right(), dot.y()));
    p.setPen(Qt::NoPen);
    p.setBrush(theme::accent());
    p.drawEllipse(dot, 6, 6);
    p.setPen(QColor(150, 150, 158));
    p.drawText(a.adjusted(6, 4, -6, -4), Qt::AlignRight | Qt::AlignBottom,
               QStringLiteral("%1 , %2").arg(m_x, 0, 'f', m_decimals).arg(m_y, 0, 'f', m_decimals));
}

// ---------------------------------------------------------------------------
// RoiEditor
// ---------------------------------------------------------------------------

RoiEditor::RoiEditor(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip(QStringLiteral("Drag a side of the rectangle to keep only part of the picture; drag inside to move it"));
}

void RoiEditor::setImage(const QImage &img)
{
    const bool resize = img.isNull() != m_image.isNull() ||
                        (!img.isNull() && std::abs(double(img.width()) / img.height() - m_aspect) > 1e-3);
    m_image = img;
    if (!img.isNull()) m_aspect = double(img.width()) / std::max(1, img.height());
    if (resize) updateGeometry();
    update();
}

void RoiEditor::setAspect(double aspect)
{
    if (aspect <= 0 || !m_image.isNull()) return;
    m_aspect = aspect;
    updateGeometry();
    update();
}

void RoiEditor::setRoi(const QRectF &r)
{
    if (m_drag >= 0 || r == m_roi) return;
    m_roi = r;
    update();
}

void RoiEditor::setPolygon(int mode, const std::array<QPointF, 4> &points)
{
    if (mode != m_roiMode) m_drag = -1;
    if (m_drag >= 0 || (mode == m_roiMode && points == m_roiPoints)) return;
    m_roiMode = mode;
    m_roiPoints = points;
    setToolTip(mode ? QStringLiteral("Drag the numbered points on the source image")
                    : QStringLiteral("Drag a side to crop the image, or drag inside to move the rectangle"));
    update();
}

QPolygonF RoiEditor::polygon() const
{
    const QRectF pic = picture();
    QPolygonF result;
    for (int i = 0; i < (m_roiMode == 1 ? 3 : 4); ++i)
        result << QPointF(pic.left() + m_roiPoints[i].x() * pic.width(), pic.top() + m_roiPoints[i].y() * pic.height());
    return result;
}

int RoiEditor::heightForWidth(int w) const { return std::clamp(int(std::lround((w - 16) / m_aspect)) + 16, 80, 400); }

QRectF RoiEditor::picture() const
{
    const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -8);
    double w = area.width(), h = w / m_aspect;
    if (h > area.height()) {
        h = area.height();
        w = h * m_aspect;
    }
    return QRectF(area.center().x() - w / 2, area.center().y() - h / 2, w, h);
}

void RoiEditor::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(24, 24, 27));
    const QRectF pic = picture();
    if (m_image.isNull()) {
        p.fillRect(pic, QColor(45, 45, 50));
        p.setPen(QColor(130, 130, 135));
        p.drawText(pic, Qt::AlignCenter, QStringLiteral("no picture yet"));
    } else {
        p.drawImage(pic, m_image);
    }
    if (m_roiMode) {
        const QPolygonF shape = polygon();
        QPainterPath outside;
        outside.addRect(pic);
        outside.addPolygon(shape);
        outside.setFillRule(Qt::OddEvenFill);
        p.fillPath(outside, QColor(0, 0, 0, 150));
        p.setPen(QPen(theme::accent(), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(shape);
        if (isEnabled()) for (int i = 0; i < shape.size(); ++i) {
            p.setBrush(theme::accent());
            p.setPen(Qt::NoPen);
            p.drawEllipse(shape[i], 8, 8);
            p.setPen(QColor(20, 20, 25));
            p.drawText(QRectF(shape[i] - QPointF(8, 8), QSizeF(16, 16)), Qt::AlignCenter, QString::number(i + 1));
        }
        return;
    }
    const QRectF c(pic.left() + m_roi.left() * pic.width(), pic.top() + m_roi.top() * pic.height(),
                   m_roi.width() * pic.width(), m_roi.height() * pic.height());
    // Unused part dimmed
    QPainterPath outside;
    outside.addRect(pic);
    outside.addRect(c);
    outside.setFillRule(Qt::OddEvenFill);
    p.fillPath(outside, QColor(0, 0, 0, 150));
    p.setPen(QPen(theme::accent(), isEnabled() ? 2 : 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(c);
    if (!isEnabled()) return;
    // Side handles
    p.setBrush(theme::accent());
    p.setPen(Qt::NoPen);
    const QPointF mids[4] = {{c.left(), c.center().y()}, {c.center().x(), c.top()}, {c.right(), c.center().y()},
                             {c.center().x(), c.bottom()}};
    for (int k = 0; k < 4; ++k) {
        const QSizeF s = (k % 2 == 0) ? QSizeF(6, 18) : QSizeF(18, 6);
        p.drawRoundedRect(QRectF(mids[k].x() - s.width() / 2, mids[k].y() - s.height() / 2, s.width(), s.height()), 2, 2);
    }
}

int RoiEditor::hit(QPointF p) const
{
    if (m_roiMode) {
        const auto shape = polygon();
        for (int i = 0; i < shape.size(); ++i)
            if (QLineF(shape[i], p).length() <= 10) return i;
        return -1;
    }
    const QRectF pic = picture();
    const QRectF c(pic.left() + m_roi.left() * pic.width(), pic.top() + m_roi.top() * pic.height(),
                   m_roi.width() * pic.width(), m_roi.height() * pic.height());
    const double tol = 7;
    const bool inY = p.y() > c.top() - tol && p.y() < c.bottom() + tol;
    const bool inX = p.x() > c.left() - tol && p.x() < c.right() + tol;
    double best = tol;
    int side = -1;
    const double d[4] = {std::abs(p.x() - c.left()), std::abs(p.y() - c.top()), std::abs(p.x() - c.right()),
                         std::abs(p.y() - c.bottom())};
    for (int k = 0; k < 4; ++k) {
        if ((k % 2 == 0 && !inY) || (k % 2 == 1 && !inX)) continue;
        if (d[k] < best) {
            best = d[k];
            side = k;
        }
    }
    if (side < 0 && c.contains(p)) side = 4;
    return side;
}

void RoiEditor::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    m_drag = hit(e->position());
    m_dragStart = m_roi;
    const QRectF pic = picture();
    m_pressNorm = QPointF((e->position().x() - pic.left()) / pic.width(), (e->position().y() - pic.top()) / pic.height());
}

void RoiEditor::mouseMoveEvent(QMouseEvent *e)
{
    const QRectF pic = picture();
    const QPointF n((e->position().x() - pic.left()) / pic.width(), (e->position().y() - pic.top()) / pic.height());
    if (m_roiMode) {
        if (m_drag < 0) {
            setCursor(hit(e->position()) >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
            return;
        }
        const QPointF point(std::clamp(n.x(), 0.0, 1.0), std::clamp(n.y(), 0.0, 1.0));
        auto points = m_roiPoints;
        points[m_drag] = point;
        if (points != m_roiPoints && validRoiPolygon(points.data(), m_roiMode == 1 ? 3 : 4)) {
            m_roiPoints = points;
            update();
            emit roiPointEdited(m_drag, point);
        }
        return;
    }
    if (m_drag < 0) {
        const int h = hit(e->position());
        setCursor(h == 0 || h == 2 ? Qt::SizeHorCursor : h == 1 || h == 3 ? Qt::SizeVerCursor : h == 4 ? Qt::SizeAllCursor
                                                                                                  : Qt::ArrowCursor);
        return;
    }
    const double minSize = 0.01;
    double l = m_dragStart.left(), t = m_dragStart.top(), r = m_dragStart.right(), b = m_dragStart.bottom();
    const double x = std::clamp(n.x(), 0.0, 1.0), y = std::clamp(n.y(), 0.0, 1.0);
    switch (m_drag) {
    case 0: l = std::min(x, r - minSize); break;
    case 1: t = std::min(y, b - minSize); break;
    case 2: r = std::max(x, l + minSize); break;
    case 3: b = std::max(y, t + minSize); break;
    case 4: {
        const double dx = std::clamp(n.x() - m_pressNorm.x(), -l, 1.0 - r);
        const double dy = std::clamp(n.y() - m_pressNorm.y(), -t, 1.0 - b);
        l += dx;
        r += dx;
        t += dy;
        b += dy;
        break;
    }
    default: break;
    }
    QRectF nc(QPointF(l, t), QPointF(r, b));
    if (m_keepProportions && m_drag >= 0 && m_drag < 4 && m_dragStart.height() > 0) {
        // The side dragged leads; the other axis follows around its center, the opposite side staying where it was
        const bool horizontal = m_drag % 2 == 0;
        nc = keepRatio(nc, m_dragStart.width() / m_dragStart.height(), horizontal ? 0 : 1);
        const QPointF center = m_dragStart.center();
        if (horizontal) {
            if (m_drag == 0) nc.moveRight(m_dragStart.right());
            else nc.moveLeft(m_dragStart.left());
            nc.moveCenter(QPointF(nc.center().x(), center.y()));
        } else {
            if (m_drag == 1) nc.moveBottom(m_dragStart.bottom());
            else nc.moveTop(m_dragStart.top());
            nc.moveCenter(QPointF(center.x(), nc.center().y()));
        }
        nc.translate(nc.left() < 0 ? -nc.left() : nc.right() > 1 ? 1 - nc.right() : 0,
                     nc.top() < 0 ? -nc.top() : nc.bottom() > 1 ? 1 - nc.bottom() : 0);
    }
    if (nc != m_roi) {
        m_roi = nc;
        update();
        emit roiEdited(m_roi);
    }
}

void RoiEditor::mouseReleaseEvent(QMouseEvent *) { m_drag = -1; }

QRectF RoiEditor::keepRatio(QRectF r, double ratio, int axis)
{
    ratio = std::max(ratio, 1e-3);
    double w = r.width(), h = r.height();
    if (axis == 0) h = w / ratio;
    else w = h * ratio;
    if (w > 1) { w = 1; h = w / ratio; }
    if (h > 1) { h = 1; w = h * ratio; }
    return QRectF(r.left(), r.top(), std::max(w, 0.01), std::max(h, 0.01));
}

// ---------------------------------------------------------------------------
// ColorEditor
// ---------------------------------------------------------------------------

ColorEditor::ColorEditor(const QString &title, const QColor &defaultColor, QWidget *parent)
    : QWidget(parent), m_color(defaultColor.toRgb()), m_default(defaultColor.toRgb())
{
    build(title);
    sync();
}

void ColorEditor::build(const QString &title)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto *head = new QHBoxLayout;
    m_switch = new FlagBox;
    m_switch->setChecked(true);
    m_switch->hide();
    head->addWidget(m_switch);
    connect(m_switch, &QCheckBox::toggled, this, &ColorEditor::switchToggled);
    auto *name = new ResetLabel(QStringLiteral("<b>%1</b>").arg(title), [this] { apply(m_default, true); });
    name->setToolTip(QStringLiteral("Click to reset the color"));
    m_swatch = new QLabel;
    m_swatch->setFixedSize(36, 16);
    m_hex = new QLineEdit;
    m_hex->setMaximumWidth(72);
    m_hex->setFixedHeight(20);
    m_hex->setToolTip(QStringLiteral("Hexadecimal value (#RRGGBB)"));
    head->addWidget(name);
    head->addStretch();
    head->addWidget(m_swatch);
    head->addWidget(m_hex);
    layout->addLayout(head);

    m_picker = new ColorPicker(false, this); // no alpha for Add / Remove
    m_picker->setSwatchVisible(false);       // the color is already shown next to its hexadecimal value
    layout->addWidget(m_picker);
    connect(m_picker, &ColorPicker::colorEdited, this, [this](const QColor &c) { apply(c, true); });
    connect(m_hex, &QLineEdit::editingFinished, this, [this] {
        const QColor c(m_hex->text().trimmed());
        if (c.isValid()) apply(c, true);
        else sync();
    });
}

void ColorEditor::apply(const QColor &color, bool emitSignal)
{
    if (!color.isValid()) return;
    QColor rgb = color.toRgb();
    rgb.setAlpha(255);
    if (rgb == m_color) {
        sync();
        return;
    }
    m_color = rgb;
    sync();
    if (emitSignal) emit colorEdited(m_color);
}

void ColorEditor::setColor(const QColor &c)
{
    apply(c, false);
}

void ColorEditor::sync()
{
    m_swatch->setStyleSheet(QStringLiteral("background:%1; border:1px solid #555; border-radius:3px;")
                                .arg(m_color.name()));
    if (!m_hex->hasFocus()) m_hex->setText(m_color.name().toUpper());
    if (m_picker) m_picker->setColor(m_color);
}

void ColorEditor::setSwitch(bool shown, bool on, const QString &tip)
{
    m_switch->setVisible(shown);
    QSignalBlocker blocker(m_switch);
    m_switch->setChecked(on);
    if (!tip.isEmpty()) m_switch->setToolTip(tip);
}

void ColorEditor::setSettingsKey(const QString &key)
{
    if (m_picker) m_picker->setSettingsKey(key);
}

// ---------------------------------------------------------------------------

QIcon padlockIcon(bool locked, bool inherited)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = locked ? QColor(235, 70, 60) : inherited ? QColor(170, 90, 80) : QColor(110, 110, 116);
    // Dark outline: readable on the selection too
    for (int pass = 0; pass < 2; ++pass) {
        const QColor col = pass == 0 ? QColor(20, 20, 22, locked || inherited ? 200 : 0) : c;
        const double grow = pass == 0 ? 1.6 : 0.0;
        p.setPen(QPen(col, 3.2 + grow * 2, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        QPainterPath shackle;
        const double x0 = 10, x1 = 22;
        const bool closed = locked || inherited;
        shackle.moveTo(x0, 16);
        shackle.lineTo(x0, closed ? 11 : 9);
        shackle.arcTo(QRectF(x0, closed ? 5 : 3, x1 - x0, 12), 180, -180);
        shackle.lineTo(x1, closed ? 16 : 11);
        p.drawPath(shackle);
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawRoundedRect(QRectF(7 - grow, 15 - grow, 18 + 2 * grow, 13 + 2 * grow), 3, 3);
    }
    return QIcon(pm);
}

QColor theme::foldArrowColor() { return QColor(238, 238, 244); }

void theme::paintFoldArrow(QPainter &p, const QPointF &center, bool open)
{
    const QPolygonF points = open ? QPolygonF{center + QPointF(-6, -3.5), center + QPointF(6, -3.5), center + QPointF(0, 4.5)}
                                  : QPolygonF{center + QPointF(-3.5, -6), center + QPointF(4.5, 0), center + QPointF(-3.5, 6)};
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 110), 1)); // a dark edge: seen on a light row too
    p.setBrush(foldArrowColor());
    p.drawPolygon(points);
    p.restore();
}

FlagBox::FlagBox(QWidget *parent) : FlagBox(QString(), parent) {}
FlagBox::FlagBox(const QString &text, QWidget *parent) : QCheckBox(text, parent)
{
    setCursor(Qt::PointingHandCursor);
    connect(theme::notifier(), &theme::Notifier::changed, this, qOverload<>(&QWidget::update));
}

QSize FlagBox::sizeHint() const
{
    const QFontMetrics fm(font());
    const int tw = text().isEmpty() ? 0 : fm.horizontalAdvance(text()) + 8;
    return QSize(16 + tw + 2, std::max(20, fm.height() + 4));
}

void FlagBox::enterEvent(QEnterEvent *e)
{
    m_hover = true;
    update();
    QCheckBox::enterEvent(e);
}

void FlagBox::leaveEvent(QEvent *e)
{
    m_hover = false;
    update();
    QCheckBox::leaveEvent(e);
}

void FlagBox::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool on = isChecked(), en = isEnabled();
    const QRectF box(1.5, (height() - 14) / 2.0, 14, 14);
    pxs::paintCheck(p, box, on, checkState() == Qt::PartiallyChecked, m_hover, en, palette()); // the same box as every check box
    if (!text().isEmpty()) {
        p.setPen(palette().color(en ? QPalette::Active : QPalette::Disabled, QPalette::WindowText));
        p.setFont(font());
        p.drawText(QRectF(box.right() + 8, 0, width() - box.right() - 8, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
}

ToggleButton::ToggleButton(const QString &text, QWidget *parent) : QToolButton(parent)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    connect(theme::notifier(), &theme::Notifier::changed, this, qOverload<>(&QWidget::update));
}

void ToggleButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool on = isChecked(), en = isEnabled();
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QColor fill = on ? theme::accent() : QColor(52, 52, 57);
    if (!on && underMouse()) fill = QColor(66, 66, 72);
    if (on && isDown()) fill = fill.darker(115);
    if (!en) fill.setAlphaF(0.45f);
    p.setPen(on ? Qt::NoPen : QPen(QColor(90, 90, 97), 1));
    p.setBrush(fill);
    p.drawRoundedRect(r, 4, 4);
    QColor fg = on ? theme::onAccent() : QColor(205, 205, 210);
    if (!en) fg.setAlphaF(0.5);
    p.setPen(fg);
    p.setFont(font());
    p.drawText(rect(), Qt::AlignCenter, text());
}

void lockInputs(QWidget *root, bool locked)
{
    if (!locked) return;
    const QList<QWidget *> all = root->findChildren<QWidget *>();
    for (QWidget *w : all) {
        if (w->property("allowLocked").toBool()) continue;
        bool parentAllowed = false;
        for (QWidget *p = w->parentWidget(); p && p != root; p = p->parentWidget()) parentAllowed |= p->property("allowLocked").toBool();
        if (parentAllowed) continue;
        if (qobject_cast<QAbstractButton *>(w) || qobject_cast<QAbstractSlider *>(w) || qobject_cast<QAbstractSpinBox *>(w) ||
            qobject_cast<QComboBox *>(w) || qobject_cast<QLineEdit *>(w) || qobject_cast<RoiEditor *>(w) ||
            qobject_cast<ResetLabel *>(w) || w->property("lockable").toBool())
            w->setEnabled(false);
    }
}

// ---------------------------------------------------------------------------
// SearchPicker
// ---------------------------------------------------------------------------

SearchPicker::SearchPicker(const QList<Item> &items, QWidget *parent) : QWidget(parent, Qt::Popup)
{
    setAttribute(Qt::WA_DeleteOnClose);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Search…"));
    m_search->setClearButtonEnabled(true);
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(12);
    v->addWidget(m_search);
    v->addWidget(m_tree, 1);
    QMap<QString, QTreeWidgetItem *> groups;
    for (const Item &it : items) {
        QTreeWidgetItem *parentItem = nullptr;
        if (!it.group.isEmpty()) {
            parentItem = groups.value(it.group);
            if (!parentItem) {
                parentItem = new QTreeWidgetItem(m_tree, {it.group});
                parentItem->setFlags(Qt::ItemIsEnabled);
                QFont f = parentItem->font(0);
                f.setItalic(true);
                parentItem->setFont(0, f);
                groups.insert(it.group, parentItem);
            }
        }
        auto *leaf = parentItem ? new QTreeWidgetItem(parentItem, {it.text}) : new QTreeWidgetItem(m_tree, {it.text});
        leaf->setData(0, Qt::UserRole, it.data);
        leaf->setToolTip(0, it.tip);
    }
    m_tree->sortItems(0, Qt::AscendingOrder);
    if (items.isEmpty()) new QTreeWidgetItem(m_tree, {QStringLiteral("(nothing in the library)")});
    connect(m_search, &QLineEdit::textChanged, this, &SearchPicker::filter);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *it) {
        const QString d = it->data(0, Qt::UserRole).toString();
        if (d.isEmpty()) return;
        emit picked(d);
        close();
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) {
        if (it->childCount()) it->setExpanded(!it->isExpanded());
        else emit m_tree->itemActivated(it, 0);
    });
    m_search->installEventFilter(this);
    resize(300, 380);
}

void SearchPicker::popup(const QPoint &globalPos)
{
    move(globalPos);
    show();
    m_search->setFocus();
}

void SearchPicker::filter(const QString &text)
{
    const QString f = text.trimmed();
    QTreeWidgetItem *first = nullptr;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *top = m_tree->topLevelItem(g);
        auto match = [&](QTreeWidgetItem *it) {
            return f.isEmpty() || it->text(0).contains(f, Qt::CaseInsensitive) || it->toolTip(0).contains(f, Qt::CaseInsensitive);
        };
        if (top->childCount() == 0) {
            top->setHidden(!match(top));
            if (!top->isHidden() && !first) first = top;
            continue;
        }
        bool any = false;
        for (int c = 0; c < top->childCount(); ++c) {
            QTreeWidgetItem *it = top->child(c);
            const bool m = match(it) || (!f.isEmpty() && top->text(0).contains(f, Qt::CaseInsensitive));
            it->setHidden(!m);
            any |= m;
            if (m && !first) first = it;
        }
        top->setHidden(!any);
        top->setExpanded(!f.isEmpty() && any);
    }
    if (first && !f.isEmpty()) m_tree->setCurrentItem(first);
}

bool SearchPicker::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_search && e->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->key() == Qt::Key_Down || k->key() == Qt::Key_Up) { // the list, while typing
            QCoreApplication::sendEvent(m_tree, e);
            return true;
        }
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            if (QTreeWidgetItem *it = m_tree->currentItem())
                if (!it->isHidden() && it->childCount() == 0) emit m_tree->itemActivated(it, 0);
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

// ---------------------------------------------------------------------------
// SliderField, RangeField
// ---------------------------------------------------------------------------

ZoomBar::ZoomBar(QWidget *view, const QString &fitTip) : QWidget(view)
{
    setStyleSheet("QWidget { background:rgba(20,20,22,200); border-radius:4px; }"
                  "QToolButton { color:#ddd; min-width:24px; padding:2px 6px; }"
                  "QToolButton:hover { background:#3a3a3e; } QToolButton:checked { background:" + theme::css() + "; color:" + theme::onAccent().name() + "; }"
                  "QLabel { color:#aaa; padding:0 4px; }");
    m_row = new QHBoxLayout(this);
    m_row->setContentsMargins(3, 2, 3, 2);
    m_row->setSpacing(1);
    QToolButton *out = addButton(QStringLiteral("−"), QStringLiteral("Zoom out (mouse wheel)"));
    m_percent = new QLabel;
    m_percent->setMinimumWidth(44);
    m_percent->setAlignment(Qt::AlignCenter);
    m_row->addWidget(m_percent);
    QToolButton *in = addButton(QStringLiteral("+"), QStringLiteral("Zoom in (mouse wheel)"));
    QToolButton *fit = addButton(QStringLiteral("Fit"), fitTip);
    connect(out, &QToolButton::clicked, this, &ZoomBar::zoomOutClicked);
    connect(in, &QToolButton::clicked, this, &ZoomBar::zoomInClicked);
    connect(fit, &QToolButton::clicked, this, &ZoomBar::fitClicked);
    m_magnet = addButton(QString(), QStringLiteral("Magnetism: dragged points, layers and viewports are caught by the "
                                                   "edges, centers and corners around them while Shift is held.\n"
                                                   "Release Shift to move freely. On or off at start: Settings."));
    m_magnet->setIcon(magnet::icon());
    m_magnet->setCheckable(true);
    m_magnet->setChecked(magnet::enabled());
    connect(m_magnet, &QToolButton::toggled, this, [](bool on) { magnet::setEnabled(on); });
    connect(magnet::notifier(), &magnet::Notifier::changed, this, [this] {
        QSignalBlocker b(m_magnet);
        m_magnet->setChecked(magnet::enabled());
    });
    m_autoSelect = addButton(QString(), QStringLiteral("Auto-select: a click in the preview selects the layer or the "
                                                       "viewport under it.\nOff: they are chosen in the list; a click "
                                                       "only moves the one selected."));
    m_autoSelect->setIcon(autoselect::icon());
    m_autoSelect->setCheckable(true);
    m_autoSelect->setChecked(autoselect::enabled());
    connect(m_autoSelect, &QToolButton::toggled, this, [](bool on) { autoselect::setEnabled(on); });
    connect(autoselect::notifier(), &autoselect::Notifier::changed, this, [this] {
        QSignalBlocker b(m_autoSelect);
        m_autoSelect->setChecked(autoselect::enabled());
    });
    connect(theme::notifier(), &theme::Notifier::changed, this, [this] {
        m_magnet->setIcon(magnet::icon());
        m_autoSelect->setIcon(autoselect::icon());
    });
    view->installEventFilter(this);
    setZoom(1.0);
}

QToolButton *ZoomBar::addButton(const QString &text, const QString &tip)
{
    auto *b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    b->setAutoRaise(true);
    m_row->addWidget(b);
    return b;
}

QToolButton *ZoomBar::addChannelButton(const std::function<void(int)> &changed)
{
    QToolButton *b = addButton(QStringLiteral("RGB"), QStringLiteral("The channel shown, in black and white: R, G, B or A (alpha). RGB: the picture as it is.\n"
                                                                     "Only the view changes: not the render, not the outputs."));
    b->setObjectName(QStringLiteral("viewChannel"));
    auto *menu = new QMenu(b);
    auto *group = new QActionGroup(menu);
    static const char *const kNames[] = {"RGB", "R", "G", "B", "A"};
    static const char *const kLong[] = {"RGB (the picture)", "R (red)", "G (green)", "B (blue)", "A (alpha)"};
    for (int k = 0; k < 5; ++k) {
        QAction *a = menu->addAction(QLatin1String(kLong[k]));
        a->setCheckable(true);
        a->setChecked(k == 0);
        group->addAction(a);
        connect(a, &QAction::triggered, b, [b, k, changed] {
            b->setText(QLatin1String(kNames[k]));
            changed(k);
        });
    }
    b->setMenu(menu);
    b->setPopupMode(QToolButton::InstantPopup);
    place();
    return b;
}

void ZoomBar::showChannel(QToolButton *button, int channel)
{
    if (!button || !button->menu()) return;
    static const char *const kNames[] = {"RGB", "R", "G", "B", "A"};
    channel = std::clamp(channel, 0, 4);
    button->setText(QLatin1String(kNames[channel]));
    const auto actions = button->menu()->actions();
    if (channel < actions.size()) actions[channel]->setChecked(true);
}

void ZoomBar::setZoom(double zoom)
{
    m_percent->setText(QStringLiteral("%1%").arg(int(std::lround(zoom * 100))));
    place();
}

bool ZoomBar::eventFilter(QObject *o, QEvent *e)
{
    if (o == parentWidget() && e->type() == QEvent::Resize) place();
    return QWidget::eventFilter(o, e);
}

void ZoomBar::place()
{
    adjustSize();
    if (QWidget *view = parentWidget()) move(view->width() - width() - 8, 8);
    raise();
}

// ---------------------------------------------------------------------------
// Transport and play-mode icons
// ---------------------------------------------------------------------------

namespace {
// 20x20 logical glyph, drawn so no symbol font is needed (as the padlock is).
// Light on the dark panel, and in the color that reads on the accent once the button is checked.
QIcon drawnIcon(const std::function<void(QPainter &, const QColor &)> &draw)
{
    QIcon icon;
    for (int px : {20, 40}) {
        for (QIcon::State st : {QIcon::Off, QIcon::On}) {
            QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(px / 20.0, px / 20.0);
            draw(p, st == QIcon::On ? theme::onAccent() : QColor(225, 225, 228));
            p.end();
            icon.addPixmap(QPixmap::fromImage(img), QIcon::Normal, st);
        }
    }
    return icon;
}

QPainterPath triangle(double x, double y, double w, double h, int dir) // dir +1 right, -1 left
{
    QPainterPath t;
    t.moveTo(x + (dir > 0 ? 0 : w), y);
    t.lineTo(x + (dir > 0 ? 0 : w), y + h);
    t.lineTo(x + (dir > 0 ? w : 0), y + h / 2);
    t.closeSubpath();
    return t;
}
} // namespace

QIcon fxMaskIcon(bool assigned, bool inverted)
{
    return drawnIcon([assigned, inverted](QPainter &p, const QColor &) {
        const QColor dark(14, 14, 17), light(248, 248, 250);
        const QColor ink = inverted ? dark : light;
        p.setPen(QPen(inverted ? light : QColor(162, 162, 171), 1));
        p.setBrush(inverted ? light : dark);
        p.drawRoundedRect(QRectF(1.5, 3.5, 17, 13), 2.5, 2.5);

        QPolygonF diamond;
        diamond << QPointF(10, 5.5) << QPointF(15, 10) << QPointF(10, 14.5) << QPointF(5, 10);
        p.setPen(assigned ? QPen(Qt::NoPen) : QPen(ink, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(assigned ? QBrush(ink) : QBrush(Qt::NoBrush));
        p.drawPolygon(diamond);
    });
}

QIcon transportIcon(TransportIcon kind)
{
    return drawnIcon([kind](QPainter &p, const QColor &c) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        switch (kind) {
        case TransportIcon::PlayForward: p.drawPath(triangle(6, 4, 9, 12, +1)); break;
        case TransportIcon::PlayBack: p.drawPath(triangle(5, 4, 9, 12, -1)); break;
        case TransportIcon::Pause:
            p.drawRect(QRectF(6, 4, 3, 12));
            p.drawRect(QRectF(11.5, 4, 3, 12));
            break;
        case TransportIcon::ToStart:
            p.drawRect(QRectF(5, 4, 2, 12));
            p.drawPath(triangle(8, 4, 7, 12, -1));
            break;
        case TransportIcon::StepBack:
            p.drawRect(QRectF(5, 4, 2, 12));
            p.drawPath(triangle(8, 6, 6, 8, -1));
            break;
        case TransportIcon::StepForward:
            p.drawPath(triangle(6, 6, 6, 8, +1));
            p.drawRect(QRectF(13, 4, 2, 12));
            break;
        case TransportIcon::MarkIn:  // [
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c, 2));
            p.drawPolyline(QPolygonF({QPointF(12, 4), QPointF(7, 4), QPointF(7, 16), QPointF(12, 16)}));
            break;
        case TransportIcon::MarkOut: // ]
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c, 2));
            p.drawPolyline(QPolygonF({QPointF(8, 4), QPointF(13, 4), QPointF(13, 16), QPointF(8, 16)}));
            break;
        }
    });
}

// PlayMode: 0 one-shot (plays once, freezes), 1 loop, 2 ping-pong, 3 stop (black at the end)
QIcon playModeIcon(int mode)
{
    return drawnIcon([mode](QPainter &p, const QColor &c) {
        p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::FlatCap));
        p.setBrush(Qt::NoBrush);
        auto arrowHead = [&](double x, double y, int dir) {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawPath(triangle(dir > 0 ? x - 4 : x, y - 3, 4, 6, dir));
            p.setPen(QPen(c, 1.6));
            p.setBrush(Qt::NoBrush);
        };
        switch (mode) {
        case 1: // loop: a circle closed by an arrow
            p.drawArc(QRectF(4, 4, 12, 12), 40 * 16, 290 * 16);
            arrowHead(15.5, 7.5, +1);
            break;
        case 2: // ping-pong: forwards above, backwards below
            p.drawLine(QPointF(4, 7), QPointF(14, 7));
            arrowHead(16, 7, +1);
            p.drawLine(QPointF(16, 13), QPointF(6, 13));
            arrowHead(4, 13, -1);
            break;
        case 3: // stop: the arrow runs into a black screen
            p.drawLine(QPointF(2, 10), QPointF(9, 10));
            arrowHead(11, 10, +1);
            p.setPen(QPen(c, 1.4));
            p.setBrush(QColor(20, 20, 22));
            p.drawRoundedRect(QRectF(12.5, 5, 6, 10), 1.5, 1.5);
            break;
        default: // one-shot: the arrow stops and stays there
            p.drawLine(QPointF(3, 10), QPointF(12, 10));
            arrowHead(14, 10, +1);
            p.setPen(QPen(c, 1.6, Qt::DotLine));
            p.drawLine(QPointF(16, 6), QPointF(16, 14));
            break;
        }
    });
}


// ---------------------------------------------------------------------------
// LevelMeter
// ---------------------------------------------------------------------------

namespace {
constexpr double kMeterFloor = -60, kMeterCeiling = 6;
double toDb(float linear) { return linear > 1e-6f ? std::max(kMeterFloor, 20.0 * std::log10(double(linear))) : kMeterFloor; }
} // namespace

LevelMeter::LevelMeter(const QString &name, QWidget *parent) : QWidget(parent), m_name(name)
{
    setObjectName(QStringLiteral("levelMeter"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); // a thin strip, as wide as it is given
}

double LevelMeter::position(double db) { return std::clamp((db - kMeterFloor) / (kMeterCeiling - kMeterFloor), 0.0, 1.0); }

void LevelMeter::setLevels(float left, float right)
{
    const float now[2] = {std::isfinite(left) ? std::max(0.f, left) : 0.f, std::isfinite(right) ? std::max(0.f, right) : 0.f};
    const qint64 t = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (int c = 0; c < 2; ++c) {
        changed |= std::abs(toDb(now[c]) - toDb(m_level[c])) > 0.2;
        m_level[c] = now[c];
        if (now[c] >= m_hold[c] || t - m_holdSince[c] > 1200) {
            changed |= m_hold[c] != now[c];
            m_hold[c] = now[c];
            m_holdSince[c] = t;
        }
    }
    if (!changed) return;
    setToolTip(QStringLiteral("%1L %2 dB   R %3 dB").arg(m_name.isEmpty() ? QString() : m_name + QStringLiteral(": "))
                   .arg(toDb(m_level[0]) <= kMeterFloor ? QStringLiteral("−∞") : QString::number(toDb(m_level[0]), 'f', 1))
                   .arg(toDb(m_level[1]) <= kMeterFloor ? QStringLiteral("−∞") : QString::number(toDb(m_level[1]), 'f', 1)));
    update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRectF area = QRectF(rect()).adjusted(0.5, 1, -0.5, -1);
    const double gap = 1.5, bar = std::max(2.0, (area.height() - gap) / 2);
    for (int c = 0; c < 2; ++c) {
        const QRectF track(area.left(), area.top() + c * (bar + gap), area.width(), bar);
        p.fillRect(track, QColor(27, 27, 29));
        const double w = track.width() * position(toDb(m_level[c]));
        const double amber = track.width() * position(-18), red = track.width() * position(-3);
        auto segment = [&](double from, double to, const QColor &color) {
            if (to > from) p.fillRect(QRectF(track.left() + from, track.top(), to - from, track.height()), color);
        };
        segment(0, std::min(w, amber), QColor(63, 174, 90));
        segment(amber, std::min(w, red), QColor(222, 178, 56));
        segment(red, w, QColor(232, 82, 70));
        const double h = track.width() * position(toDb(m_hold[c]));
        if (m_hold[c] > 0.001f) p.fillRect(QRectF(track.left() + std::min(h, track.width() - 1.5), track.top(), 1.5, track.height()),
                                           m_hold[c] > 1.0f ? QColor(232, 82, 70) : QColor(205, 205, 210));
    }
    // Where 0 dBFS is
    const double zero = area.left() + area.width() * position(0);
    p.setPen(QColor(90, 90, 96));
    p.drawLine(QPointF(zero, area.top()), QPointF(zero, area.bottom()));
}
