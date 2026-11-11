#pragma once

#include <QAbstractItemView>
#include <QEvent>
#include <QHeaderView>
#include <QScopedValueRollback>
#include <vector>

// Every grip edits the column on its left. With no horizontal scrolling,
// remaining columns share the change needed to keep the table inside its pane.
class FittedTableHeader : public QObject
{
public:
    FittedTableHeader(QAbstractItemView *view, QHeaderView *header, int fillColumn)
        : QObject(view), m_view(view), m_header(header), m_fillColumn(fillColumn)
    {
        header->setStretchLastSection(false);
        header->setSectionResizeMode(QHeaderView::Interactive);
        header->setCascadingSectionResizes(false);
        for (int column = 0; column < header->count(); ++column)
            m_preferred.push_back(qMax(header->minimumSectionSize(), header->sectionSize(column)));
        connect(header, &QHeaderView::sectionResized, this, [this](int column, int oldWidth, int newWidth) {
            if (m_fitting || !m_header->isVisible()) return;
            if (!oldWidth || !newWidth) {
                fit(-1);
                return;
            }
            fit(column);
            for (int i = 0; i < m_header->count(); ++i)
                if (!m_header->isSectionHidden(i)) m_preferred[size_t(i)] = m_header->sectionSize(i);
        });
        view->viewport()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object == m_view->viewport() && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
            fit(-1);
        return QObject::eventFilter(object, event);
    }

private:
    void fit(int edited)
    {
        if (m_fitting) return;
        const QScopedValueRollback<bool> guard(m_fitting, true);
        const int available = m_view->viewport()->width();
        if (available <= 0) return;

        std::vector<int> others;
        const int pivot = edited >= 0 ? m_header->visualIndex(edited) : -1;
        for (int offset = 1; offset <= m_header->count(); ++offset) {
            const int column = m_header->logicalIndex((pivot + offset) % m_header->count());
            if (column != edited && !m_header->isSectionHidden(column)) others.push_back(column);
        }
        if (others.empty()) return;
        if (edited < 0) {
            const int minimum = m_header->minimumSectionSize();
            const int room = qMax(0, available - minimum * int(others.size()));
            int totalWeight = 0;
            for (int column : others) totalWeight += m_preferred[size_t(column)];
            int allocated = 0;
            for (int column : others) {
                const int extra = totalWeight > 0 ? int(qint64(room) * m_preferred[size_t(column)] / totalWeight) : 0;
                m_header->resizeSection(column, minimum + extra);
                allocated += extra;
            }
            const int fill = m_header->isSectionHidden(m_fillColumn) ? others.back() : m_fillColumn;
            if (allocated < room) m_header->resizeSection(fill, m_header->sectionSize(fill) + room - allocated);
            return;
        }

        int remaining = available - m_header->length();
        if (remaining > 0) {
            const int column = others.front();
            m_header->resizeSection(column, m_header->sectionSize(column) + remaining);
            return;
        }
        others.push_back(edited);
        for (int column : others) {
            const int width = m_header->sectionSize(column);
            const int reduction = qMin(-remaining, qMax(0, width - m_header->minimumSectionSize()));
            if (reduction > 0) m_header->resizeSection(column, width - reduction);
            remaining += reduction;
            if (remaining >= 0) break;
        }
    }

    QAbstractItemView *m_view;
    QHeaderView *m_header;
    int m_fillColumn;
    std::vector<int> m_preferred;
    bool m_fitting = false;
};
