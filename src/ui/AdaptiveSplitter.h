#pragma once

#include <QResizeEvent>
#include <QSplitter>

// Stack panes when there is no room for both columns. Keep their minimum
// width independent of the current orientation so the parent can shrink.
class AdaptiveSplitter : public QSplitter
{
public:
    explicit AdaptiveSplitter(int columnsWidth) : QSplitter(Qt::Horizontal), m_columnsWidth(columnsWidth)
    {
        setChildrenCollapsible(false);
    }

    QSize minimumSizeHint() const override
    {
        QSize hint = QSplitter::minimumSizeHint();
        int width = 0;
        for (int i = 0; i < count(); ++i) {
            const QWidget *pane = widget(i);
            if (!pane->isHidden())
                width = qMax(width, qMax(pane->minimumWidth(), pane->minimumSizeHint().width()));
        }
        hint.setWidth(width);
        return hint;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        const Qt::Orientation desired = event->size().width() < m_columnsWidth ? Qt::Vertical : Qt::Horizontal;
        if (orientation() != desired) {
            setOrientation(desired);
            if (count() == 2) setSizes({desired == Qt::Vertical ? 120 : 280, 600});
        }
        QSplitter::resizeEvent(event);
    }

private:
    int m_columnsWidth;
};
