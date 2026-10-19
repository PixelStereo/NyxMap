#pragma once
#include <QGraphicsView>
#include <functional>

// Trackpads send native magnification on macOS/Wayland, or a Qt pinch gesture
// on other touch systems. Both paths use the same bounded, anchored zoom.
class ModulationGraphView : public QGraphicsView
{
  public:
    explicit ModulationGraphView(QGraphicsScene *scene, QWidget *parent = nullptr);
    void zoomAt(const QPointF &viewportPoint, double factor);
    void fitGraph(const QRectF &bounds);
    double zoom() const { return transform().m11(); }
    std::function<void(double)> zoomChanged;

  protected:
    bool event(QEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

  private:
    bool handleGesture(QEvent *event);
    bool m_nativePinching = false;
    double m_lastPinchTotal = 1.;
};
