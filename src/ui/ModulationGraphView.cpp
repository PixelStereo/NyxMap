#include "ModulationGraphView.h"
#include <QGestureEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPinchGesture>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

ModulationGraphView::ModulationGraphView(QGraphicsScene *scene, QWidget *parent) : QGraphicsView(scene, parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    // Ports extend beyond node bodies and animated values repaint frequently.
    // Repaint the canvas as one surface, including on Retina displays.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::RubberBandDrag);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
#ifndef Q_OS_MACOS
    // macOS already delivers native magnification. Registering Qt's recognizer
    // here would consume those native events and synthesize a second gesture path.
    viewport()->grabGesture(Qt::PinchGesture);
#endif
}

void ModulationGraphView::zoomAt(const QPointF &point, double factor)
{
    if (!std::isfinite(factor) || factor <= 0)
        return;
    const double current = zoom();
    const double target = std::clamp(current * factor, .2, 3.5);
    const QPointF before = mapToScene(point.toPoint());
    scale(target / current, target / current);
    const QPointF after = mapToScene(point.toPoint());
    translate(after.x() - before.x(), after.y() - before.y());
    if (zoomChanged)
        zoomChanged(zoom());
}

void ModulationGraphView::fitGraph(const QRectF &bounds)
{
    if (bounds.isEmpty())
        return;
    fitInView(bounds.adjusted(-32, -32, 32, 32), Qt::KeepAspectRatio);
    const double bounded = std::clamp(zoom(), .2, 1.);
    if (std::abs(bounded - zoom()) > 1e-9)
        zoomAt(viewport()->rect().center(), bounded / zoom());
    if (zoomChanged)
        zoomChanged(zoom());
}

bool ModulationGraphView::handleGesture(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture)
    {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        switch (gesture->gestureType())
        {
        case Qt::BeginNativeGesture:
            m_nativePinching = true;
            break;
        case Qt::EndNativeGesture:
            m_nativePinching = false;
            break;
        case Qt::ZoomNativeGesture:
            zoomAt(gesture->position(), 1. + gesture->value());
            break;
        default:
            return false;
        }
        event->accept();
        return true;
    }
    if (event->type() == QEvent::Gesture)
    {
        auto *gestures = static_cast<QGestureEvent *>(event);
        if (auto *pinch = static_cast<QPinchGesture *>(gestures->gesture(Qt::PinchGesture)))
        {
            if (!m_nativePinching && pinch->changeFlags().testFlag(QPinchGesture::ScaleFactorChanged))
            {
                if (pinch->state() == Qt::GestureStarted)
                    m_lastPinchTotal = 1.;
                const double total = pinch->totalScaleFactor();
                if (std::isfinite(total) && total > 0)
                {
                    zoomAt(pinch->centerPoint(), total / m_lastPinchTotal);
                    m_lastPinchTotal = total;
                }
            }
            if (pinch->state() == Qt::GestureFinished || pinch->state() == Qt::GestureCanceled)
                m_lastPinchTotal = 1.;
            gestures->accept(pinch);
            return true;
        }
    }
    return false;
}

bool ModulationGraphView::event(QEvent *event) { return handleGesture(event) || QGraphicsView::event(event); }

bool ModulationGraphView::viewportEvent(QEvent *event)
{
    return handleGesture(event) || QGraphicsView::viewportEvent(event);
}

void ModulationGraphView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier))
    {
        const double delta =
            event->pixelDelta().isNull() ? event->angleDelta().y() / 120. : event->pixelDelta().y() / 40.;
        zoomAt(event->position(), std::pow(1.15, delta));
        event->accept();
        return;
    }
    // Ordinary two-finger scrolling retains Qt's horizontal/vertical panning.
    QGraphicsView::wheelEvent(event);
}

void ModulationGraphView::drawBackground(QPainter *painter, const QRectF &rect)
{
    const QColor window = palette().window().color();
    const bool dark = window.lightness() < 128;
    const QColor background = dark ? window.darker(125) : window;
    painter->fillRect(rect, background);
    QColor dots = dark ? window.lighter(185) : window.darker(125);
    dots.setAlpha(90);
    painter->setPen(QPen(dots, 0));
    double spacing = 24;
    while (spacing * zoom() < 14)
        spacing *= 2;
    const double left = std::floor(rect.left() / spacing) * spacing;
    const double top = std::floor(rect.top() / spacing) * spacing;
    for (double y = top; y <= rect.bottom(); y += spacing)
        for (double x = left; x <= rect.right(); x += spacing)
            painter->drawPoint(QPointF(x, y));
}
