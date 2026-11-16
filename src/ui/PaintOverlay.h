#pragma once
// A transparent child over a QRhiWidget that lets the parent draw with a QPainter (names, handles' labels, fixtures):
// a QRhiWidget has no QPainter of its own. It covers its parent, ignores the mouse, and repaints when asked.
#include <QPainter>
#include <QWidget>
#include <functional>

class PaintOverlay : public QWidget
{
public:
    PaintOverlay(QWidget *parent, std::function<void(QPainter &)> paint) : QWidget(parent), m_paint(std::move(paint))
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setGeometry(parent->rect());
    }
    // The parent calls it from its resizeEvent
    void follow() { setGeometry(parentWidget()->rect()); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        m_paint(p);
    }

private:
    std::function<void(QPainter &)> m_paint;
};
