#include "LayerTable.h"
#include <QSet>
#include "Widgets.h"
#include "../engine/Layer.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <memory>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QShortcut>
#include <QSignalBlocker>
#include <QApplication>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <tuple>

enum Col { ColHandle, ColVisible, ColLock, ColSolo, ColName, ColBlend, ColOpacity, ColAudio, ColCount };
enum NameRole { RoleMeta = Qt::UserRole + 1, RoleDepth, RoleArrow, RoleTag, RoleThumb, RoleMetaHead, RolePlayback, RoleMetaTail };

static const char *const kRowsMime = kLayerRowsMime;
static constexpr int kArrowZone = 22;  // px at the left of a group's name: fold / unfold
static constexpr int kIndent = 26;     // px: members of a group

namespace {
// The name cell: the layer's small picture (or its kind's tag), its name in bold and, under it, what it is and how it plays
// The S of the solo column keeps its own colour in a selected row (dim: off, yellow: soloed); the selection's white would
// look lit
class SoloDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const QBrush own = index.data(Qt::ForegroundRole).value<QBrush>();
        if (own.style() != Qt::NoBrush) option->palette.setBrush(QPalette::HighlightedText, own);
    }
};

class NameDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &ix) const override
    {
        QStyleOptionViewItem o = opt;
        initStyleOption(&o, ix);
        o.text.clear();
        o.icon = QIcon();
        QStyle *st = o.widget ? o.widget->style() : QApplication::style();
        st->drawControl(QStyle::CE_ItemViewItem, &o, p, o.widget);
        p->save();
        p->setClipRect(opt.rect);
        const int depth = ix.data(RoleDepth).toInt();
        int x = opt.rect.left() + 8 + depth * kIndent;
        const QString arrow = ix.data(RoleArrow).toString();
        const QColor name = ix.data(Qt::ForegroundRole).value<QBrush>().color();
        if (!arrow.isEmpty()) { // the same fold mark as every list's
            theme::paintFoldArrow(*p, QPointF(x + (kArrowZone - 6) / 2.0, opt.rect.center().y() + 0.5), arrow == QStringLiteral("▾"));
            x += kArrowZone - 4;
        }
        const QRect box(x, opt.rect.top() + 5, 56, opt.rect.height() - 10);
        const QPixmap thumb = ix.data(RoleThumb).value<QPixmap>();
        p->fillRect(box, QColor(18, 18, 22));
        if (!thumb.isNull()) {
            const QSize sz = thumb.size().scaled(box.size(), Qt::KeepAspectRatio);
            p->drawPixmap(QRect(box.center() - QPoint(sz.width() / 2, sz.height() / 2), sz), thumb);
        } else {
            p->setPen(name);
            p->drawText(box, Qt::AlignCenter, ix.data(RoleTag).toString());
        }
        p->setPen(QColor(70, 70, 78));
        p->drawRect(box.adjusted(0, 0, -1, -1));
        const int tx = box.right() + 9;
        const QRect textBox(tx, opt.rect.top(), opt.rect.right() - tx - 4, opt.rect.height());
        QFont f = opt.font;
        f.setBold(true);
        QFontMetrics fm(f);
        p->setFont(f);
        p->setPen(name);
        p->drawText(QRect(textBox.left(), textBox.top() + 5, textBox.width(), fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(ix.data(Qt::DisplayRole).toString(), Qt::ElideRight, textBox.width()));
        QFont g = opt.font;
        g.setPointSizeF(std::max(7.0, g.pointSizeF() - 1.5));
        QFontMetrics gm(g);
        p->setFont(g);
        p->setPen(QColor(140, 140, 150));
        const QRect metaLine(textBox.left(), textBox.top() + 6 + fm.height(), textBox.width(), gm.height());
        const QString head = ix.data(RoleMetaHead).toString(), tail = ix.data(RoleMetaTail).toString();
        const QStringList playback = ix.data(RolePlayback).toString().split(QLatin1Char('\x1f'));
        if (playback.size() != 4) { // nothing that runs: one line of text
            p->drawText(metaLine, Qt::AlignLeft | Qt::AlignVCenter,
                        gm.elidedText(ix.data(RoleMeta).toString(), Qt::ElideRight, textBox.width()));
        } else {
            // What runs sits in spaces of fixed widths (the widest glyphs and digits), so that the line does not move
            auto widest = [&](const QStringList &candidates) {
                int w = 0;
                for (const QString &c : candidates) w = std::max(w, gm.horizontalAdvance(c));
                return w;
            };
            int digit = 0;
            for (QChar c = QLatin1Char('0'); c <= QLatin1Char('9'); c = QChar(c.unicode() + 1)) digit = std::max(digit, gm.horizontalAdvance(c));
            const int stateW = widest({QStringLiteral("■"), QStringLiteral("❚❚"), QStringLiteral("◀"), QStringLiteral("▶")});
            const int modeW = widest({QStringLiteral("→|"), QStringLiteral("↻"), QStringLiteral("⇄"), QStringLiteral("→■")});
            const int colon = gm.horizontalAdvance(QLatin1Char(':')), dot = gm.horizontalAdvance(QLatin1Char('.')), gap = gm.horizontalAdvance(QLatin1Char(' '));
            const int positionW = 4 * digit + colon + 2 * digit; // 00:00, room for a hundred minutes
            const int durationW = 4 * digit + colon + dot + 2 * digit + 2 * digit;
            int x = metaLine.left();
            auto put = [&](const QString &text, int width, Qt::Alignment align = Qt::AlignLeft) {
                p->drawText(QRect(x, metaLine.top(), width, metaLine.height()), align | Qt::AlignVCenter, text);
                x += width;
            };
            if (!head.isEmpty()) put(head + QStringLiteral(" · "), gm.horizontalAdvance(head + QStringLiteral(" · ")));
            put(playback[0], stateW);
            x += gap;
            put(playback[1], positionW, Qt::AlignRight);
            put(QStringLiteral(" / "), gm.horizontalAdvance(QStringLiteral(" / ")));
            put(playback[2], durationW);
            x += gap;
            put(playback[3], modeW);
            if (!tail.isEmpty() && x < metaLine.right())
                p->drawText(QRect(x, metaLine.top(), metaLine.right() - x, metaLine.height()), Qt::AlignLeft | Qt::AlignVCenter,
                            gm.elidedText(QStringLiteral(" · ") + tail, Qt::ElideRight, metaLine.right() - x));
        }
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return QSize(160, 44); }
};

// Rename in place: Enter validates, Esc cancels
class RenameEdit : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;
    bool cancelled = false;

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape) {
            cancelled = true;
            clearFocus();
            return;
        }
        QLineEdit::keyPressEvent(e);
    }
};

// Layer grid: files dropped onto a row load into it; rows dragged within the list move layers (between rows)
// or put them into a group (onto the group's row).
class LayerGrid : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;
    int anchor() const { return m_anchor; }
    void setAnchor(int row) { m_anchor = row; }
    void cancelPendingPress() { m_pendingColumn = -1; m_pressRow = -1; m_rangePress = false; }
    const std::vector<LayerTable::Row> *rows = nullptr;
    std::function<void(int, const QStringList &)> onDrop;
    std::function<void(const QList<int> &, int, int)> onMove;
    std::function<void(int)> onCollapse, onRename;

protected:
    struct Target {
        int before = -1, parent = -1;
        int into = -1;   // group row highlighted (drop into it)
        int lineY = -1;  // otherwise: insertion line
        int indent = 0;
    };

    static QStringList paths(const QMimeData *m)
    {
        QStringList out;
        for (const QUrl &u : m->urls())
            if (u.isLocalFile()) out << u.toLocalFile();
        return out;
    }

    // The fold arrow of a group, indented with it
    bool onArrow(int row, int x) const
    {
        const LayerTable::Row &r = (*rows)[size_t(row)];
        const int dx = x - columnViewportPosition(ColName) - r.depth * kIndent;
        return r.group && dx >= 0 && dx < kArrowZone;
    }

    int lastVisibleRow() const
    {
        for (int r = rowCount() - 1; r >= 0; --r)
            if (!isRowHidden(r)) return r;
        return -1;
    }

    // Where dragged rows would go: before which row, into which group (-1: top level)
    Target target(QPoint pos) const
    {
        Target t;
        const auto &R = *rows;
        const int n = int(R.size());
        auto depth = [&](int r) { return r >= 0 && r < n ? R[size_t(r)].depth : -1; };
        auto blockEnd = [&](int g) { // the row after a group and everything inside it
            int k = g + 1;
            while (k < n && depth(k) > depth(g)) ++k;
            return k;
        };
        auto parentAt = [&](int r, int level) { // the group at `level - 1` above r (r at depth >= level)
            int p = -1;
            for (int k = r - 1; k >= 0 && level > 0; --k)
                if (depth(k) == level - 1) {
                    p = k;
                    break;
                }
            return p;
        };
        const int nameX = columnViewportPosition(ColName);
        const int row = rowAt(pos.y());
        if (row < 0 || row >= n) {
            const int last = lastVisibleRow();
            t.lineY = last >= 0 ? rowViewportPosition(last) + rowHeight(last) : 0;
            return t; // at the end, top level
        }
        const int y0 = rowViewportPosition(row), h = rowHeight(row);
        const double rel = double(pos.y() - y0) / std::max(1, h);
        const LayerTable::Row &r = R[size_t(row)];
        const int d = r.depth;
        if (r.group && rel > 0.25 && rel < 0.75) { // onto a group: at the end of its contents
            const int end = blockEnd(row);
            t.into = row;
            t.parent = row;
            t.before = end < n ? end : -1;
            return t;
        }
        if (rel < (r.group ? 0.25 : 0.5)) { // above the row, at its level
            t.before = row;
            t.parent = parentAt(row, d);
            t.lineY = y0;
            t.indent = d * kIndent;
            return t;
        }
        t.lineY = y0 + h;
        if (r.group && !r.collapsed && blockEnd(row) > row + 1) { // below an open group: first in it
            t.before = row + 1;
            t.parent = row;
            t.indent = (d + 1) * kIndent;
            return t;
        }
        // Below the row (a folded group: below its contents). At the end of a group, the pointer's
        // horizontal position chooses the level: further left leaves the group.
        const int next = r.group ? blockEnd(row) : row + 1;
        const int nextDepth = next < n ? depth(next) : 0;
        int level = d;
        if (nextDepth < d) level = std::clamp((pos.x() - nameX) / kIndent, nextDepth, d);
        t.before = next < n ? next : -1;
        t.parent = parentAt(row + 1, level);
        t.indent = level * kIndent;
        return t;
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        const QPoint p = e->position().toPoint();
        const int row = rowAt(p.y()), col = columnAt(p.x());
        cancelPendingPress();
        if (row < 0) { // empty area: nothing selected (a new group is then created empty)
            clearSelection();
            setCurrentCell(-1, -1);
            return;
        }
        if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier)) {
            const int anchor = m_anchor >= 0 && m_anchor < rowCount() ? m_anchor : std::max(0, currentRow());
            const int previous = currentRow(), previousColumn = currentColumn();
            {
                QSignalBlocker block(this);
                QItemSelection range;
                for (int r = std::min(anchor, row); r <= std::max(anchor, row); ++r)
                    if (!isRowHidden(r)) range.select(model()->index(r, 0), model()->index(r, columnCount() - 1));
                selectionModel()->setCurrentIndex(model()->index(row, std::max(0, col)), QItemSelectionModel::NoUpdate);
                selectionModel()->select(range, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
            m_rangePress = true;
            emit currentCellChanged(row, col, previous, previousColumn);
            emit itemSelectionChanged();
            emit clicked(model()->index(row, std::max(0, col)));
            return;
        }
        if (e->button() == Qt::LeftButton && col == ColName && rows && row < int(rows->size()) && onArrow(row, p.x())) {
            if (onCollapse) onCollapse(row);
            return;
        }
        if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)) {
            const int previous = currentRow(), previousColumn = currentColumn();
            auto *selection = selectionModel();
            const bool remove = selection->isRowSelected(row, QModelIndex());
            int current = row;
            {
                const QSignalBlocker block(this);
                selection->select(model()->index(row, 0), QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
                if (remove) {
                    const auto remaining = selection->selectedRows();
                    current = previous >= 0 && selection->isRowSelected(previous, QModelIndex()) ? previous
                              : remaining.isEmpty() ? -1 : remaining.front().row();
                }
                selection->setCurrentIndex(current < 0 ? QModelIndex() : model()->index(current, ColName),
                                           QItemSelectionModel::NoUpdate);
            }
            m_anchor = current;
            m_rangePress = true;
            emit currentCellChanged(current, ColName, previous, previousColumn);
            emit itemSelectionChanged();
            return;
        }
        if (e->button() == Qt::LeftButton && col != ColVisible && col != ColLock && col != ColSolo) {
            m_pressRow = row;
            m_pressPos = p;
            // A plain click selects on release: a press that becomes a drag (onto the inspector's source, say)
            // leaves the selection, and the layer shown, as they are
            if (e->modifiers() == Qt::NoModifier) {
                m_pendingColumn = col;
                setFocus(Qt::MouseFocusReason);
                return;
            }
        }
        if (e->button() == Qt::LeftButton && !(e->modifiers() & Qt::ShiftModifier)) m_anchor = row;
        QTableWidget::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!e->buttons()) { // over a handle: a hand, it can be dragged
            const QPoint p = e->position().toPoint();
            viewport()->setCursor(rowAt(p.y()) >= 0 && columnAt(p.x()) == ColHandle ? Qt::OpenHandCursor : Qt::ArrowCursor);
        }
        if (m_pressRow >= 0 && (e->buttons() & Qt::LeftButton) &&
            (e->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            QList<int> dragged;
            for (const QModelIndex &i : selectionModel()->selectedRows()) dragged << i.row();
            if (!dragged.contains(m_pressRow)) dragged = {m_pressRow};
            std::sort(dragged.begin(), dragged.end());
            QStringList list;
            for (int r : dragged) list << QString::number(r);
            auto *mime = new QMimeData;
            mime->setData(kRowsMime, list.join(',').toUtf8());
            auto *drag = new QDrag(this);
            drag->setMimeData(mime);
            m_pressRow = -1;
            m_pendingColumn = -1;
            drag->exec(Qt::MoveAction);
            return;
        }
        if (m_pendingColumn < 0 && !m_rangePress) QTableWidget::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const int row = m_pressRow, col = m_pendingColumn;
        m_pressRow = m_pendingColumn = -1;
        if (m_rangePress) { m_rangePress = false; return; }
        if (row >= 0 && col >= 0 && e->button() == Qt::LeftButton) {
            // Commit one row explicitly. Qt's extended selection can defer
            // deselection on an already selected row until release.
            if (rowAt(e->position().toPoint().y()) != row) return;
            const auto index = model()->index(row, col);
            const int previous = currentRow(), previousColumn = currentColumn();
            m_anchor = row;
            {
                const QSignalBlocker block(this);
                selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
            if (row != previous || col != previousColumn)
                emit currentCellChanged(row, col, previous, previousColumn);
            emit itemSelectionChanged();
            emit clicked(index);
            return;
        }
        QTableWidget::mouseReleaseEvent(e);
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        cancelPendingPress();
        const QPoint p = e->position().toPoint();
        const int row = rowAt(p.y()), col = columnAt(p.x());
        if (row >= 0 && col == ColName && rows && row < int(rows->size())) {
            if (onArrow(row, p.x())) {
                if (onCollapse) onCollapse(row);
            } else if (onRename) {
                onRename(row);
            }
            return;
        }
        QTableWidget::mouseDoubleClickEvent(e);
    }

    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasFormat(kRowsMime) || !paths(e->mimeData()).isEmpty()) e->acceptProposedAction();
        else e->ignore();
    }

    void dragMoveEvent(QDragMoveEvent *e) override
    {
        const QPoint p = e->position().toPoint();
        if (e->mimeData()->hasFormat(kRowsMime)) {
            m_target = target(p);
            m_internal = true;
            m_dropRow = -2;
        } else {
            m_internal = false;
            m_dropRow = rowAt(p.y());
        }
        viewport()->update();
        e->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent *) override
    {
        m_dropRow = -2;
        m_internal = false;
        viewport()->update();
    }

    void dropEvent(QDropEvent *e) override
    {
        const QPoint p = e->position().toPoint();
        const bool internal = e->mimeData()->hasFormat(kRowsMime);
        m_dropRow = -2;
        m_internal = false;
        viewport()->update();
        e->acceptProposedAction();
        if (internal) {
            QList<int> moved;
            for (const QByteArray &b : e->mimeData()->data(kRowsMime).split(',')) moved << b.toInt();
            const Target t = target(p);
            // Deferred: the drag is still running in this call
            QTimer::singleShot(0, this, [this, moved, t] {
                if (onMove) onMove(moved, t.before, t.parent);
            });
            return;
        }
        if (onDrop) onDrop(rowAt(p.y()), paths(e->mimeData()));
    }

    void paintEvent(QPaintEvent *e) override
    {
        QTableWidget::paintEvent(e);
        QPainter p(viewport());
        const QColor accent = theme::accent();
        if (rows && rows->empty()) { // nothing yet: say what to do
            p.setPen(QColor(136, 136, 142));
            p.drawText(viewport()->rect().adjusted(16, 16, -16, -16), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                       QStringLiteral("No layers yet.\nClick + Layer, or drop a video, an image, a sound or a shader here."));
        }
        // Each row a band of its kind's colour; each group a fine line around all it holds
        if (rows) {
            const int bandX = columnViewportPosition(ColName), width = viewport()->width();
            for (int r = 0; r < int(rows->size()); ++r) {
                const LayerTable::Row &row = (*rows)[size_t(r)];
                if (isRowHidden(r) || !row.color.isValid()) continue;
                QColor band = row.color;
                if (row.soloHidden) band.setAlpha(70);
                const int indent = row.depth * 10;
                p.fillRect(bandX + indent, rowViewportPosition(r) + 1, 3, rowHeight(r) - 2, band);
            }
            for (int r = 0; r < int(rows->size()); ++r) {
                const LayerTable::Row &row = (*rows)[size_t(r)];
                if (!row.group || isRowHidden(r)) continue;
                int last = r;
                for (int k = r + 1; k < int(rows->size()) && (*rows)[size_t(k)].depth > row.depth; ++k)
                    if (!isRowHidden(k)) last = k;
                QColor line = row.color;
                line.setAlpha(170);
                p.setPen(QPen(line, 1));
                p.setBrush(Qt::NoBrush);
                const int inset = 1 + row.depth * 3, top = rowViewportPosition(r);
                p.drawRect(inset, top, width - 2 * inset - 1, rowViewportPosition(last) + rowHeight(last) - top - 1);
            }
        }
        // The viewports, a block of their own at the top
        if (rows) {
            int lastVp = -1;
            for (int r = 0; r < int(rows->size()) && (*rows)[size_t(r)].viewport; ++r) lastVp = r;
            if (lastVp >= 0 && lastVp + 1 < int(rows->size())) {
                // A soft line: fading out at both ends, so the block reads as its own without a hard rule
                const int y = rowViewportPosition(lastVp) + rowHeight(lastVp);
                const int w = viewport()->width();
                QColor mid = palette().color(QPalette::Text);
                mid.setAlpha(120);
                QColor edge = mid;
                edge.setAlpha(0);
                QLinearGradient fade(QPointF(10, 0), QPointF(w - 10, 0));
                fade.setColorAt(0, edge);
                fade.setColorAt(0.5, mid);
                fade.setColorAt(1, edge);
                p.setPen(Qt::NoPen);
                p.setBrush(fade);
                p.drawRect(QRectF(0, y - 1, w, 2));
            }
        }
        if (m_internal) {
            if (m_target.into >= 0) {
                p.setPen(QPen(accent, 2));
                { QColor a = theme::accent(); a.setAlpha(40); p.setBrush(a); }
                const int y = rowViewportPosition(m_target.into);
                p.drawRect(QRect(1, y + 1, viewport()->width() - 3, rowHeight(m_target.into) - 3));
            } else if (m_target.lineY >= 0) {
                const int x = columnViewportPosition(ColName) + m_target.indent;
                p.setPen(QPen(accent, 3));
                p.drawLine(x, m_target.lineY, viewport()->width() - 4, m_target.lineY);
                p.setBrush(accent);
                p.drawEllipse(QPoint(x, m_target.lineY), 4, 4);
            }
            return;
        }
        if (m_dropRow < 0) return;
        p.setPen(QPen(accent, 2));
        { QColor a = theme::accent(); a.setAlpha(40); p.setBrush(a); }
        const int y = rowViewportPosition(m_dropRow);
        p.drawRect(QRect(1, y + 1, viewport()->width() - 3, rowHeight(m_dropRow) - 3));
    }

private:
    int m_dropRow = -2;
    bool m_internal = false;
    Target m_target;
    int m_pressRow = -1;
    QPoint m_pressPos;
    int m_anchor = -1;
    bool m_rangePress = false;
    int m_pendingColumn = -1; // a plain click, selecting on release unless it became a drag
};
} // namespace

static QToolButton *barButton(const QString &text, const QString &tip)
{
    auto *b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setMinimumHeight(24);
    b->setMinimumWidth(text.size() <= 2 ? 30 : 0); // a symbol is a square; a word has its own width
    return b;
}

LayerTable::LayerTable(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(8, 4, 8, 4);
    v->setSpacing(4);

    // What to show: all the layers, or only the videos (pictures), the 3D ones or the sounds, as the Media Bin does
    {
        struct Pill { const char *name; const char *colour; const char *tip; };
        static const Pill pills[6] = {{"All", "#a3afc6", "Show every layer and the fixtures"},
                                      {"Video", "#a598cf", "Show the videos, images, texts and live inputs"},
                                      {"3D", "#6fb9c4", "Show the 3D scenes, models, lights, cameras and paths"},
                                      {"Audio", "#62ad80", "Show the sounds and the audio inputs"},
                                      {"Light", "#d3bb7c", "Show the list of the fixtures (DMX lights), under the layers"},
                                      {"ISF", "#7ba2d8", "Show the ISF generators"}};
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        // Video, ISF, 3D and Audio add up (each is a bit of m_filter, as its category); Light shows or hides the fixtures' list;
        // All selects them all, or takes them all away
        auto *buttons = new QButtonGroup(this);
        buttons->setExclusive(false);
        for (int i : {0, 1, 5, 2, 3, 4}) { // shown: All, Video, ISF, 3D, Audio, Light
            auto *pill = new QToolButton;
            pill->setObjectName(QStringLiteral("layerPill%1").arg(QLatin1String(pills[i].name)));
            pill->setCheckable(true);
            pill->setChecked(true); // everything shown at first
            pill->setText(QLatin1String(pills[i].name));
            pill->setToolTip(QLatin1String(pills[i].tip));
            pill->setCursor(Qt::PointingHandCursor);
            pill->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            const QString c = QLatin1String(pills[i].colour);
            pill->setStyleSheet(QStringLiteral("QToolButton { background:#35353b; color:%1; border:1px solid #4a4a52; border-bottom:3px solid %1; "
                                               "border-radius:8px; padding:4px 10px; font-weight:600; }"
                                               "QToolButton:hover { background:#404048; }"
                                               "QToolButton:checked { background:%1; color:#16161b; }").arg(c));
            buttons->addButton(pill, i);
            row->addWidget(pill);
            m_filterPills[size_t(i)] = pill;
        }
        connect(buttons, &QButtonGroup::idClicked, this, [this](int id) {
            if (id == 0) { // All: every kind follows it
                const bool on = m_filterPills[0]->isChecked();
                for (int k = 1; k < 6; ++k) m_filterPills[size_t(k)]->setChecked(on);
            } else {
                bool all = true;
                for (int k = 1; k < 6; ++k) all &= m_filterPills[size_t(k)]->isChecked();
                m_filterPills[0]->setChecked(all);
            }
            int mask = 0;
            for (int k : {1, 2, 3, 5})
                if (m_filterPills[size_t(k)]->isChecked()) mask |= 1 << k;
            setFilter(mask);
            emit fixturesShownChanged(m_filterPills[4]->isChecked());
        });
        v->addLayout(row);
    }
    auto *bar = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("<b>Layers</b>"));
    auto *add = barButton(QStringLiteral("+"), QStringLiteral("New Layer (empty: drop a media onto it)"));
    auto *group = barButton(QStringLiteral("Group"), QStringLiteral("New Group (Ctrl+G): the selected layers go into it"));
    auto *scene = barButton(QStringLiteral("3D"), QStringLiteral("3D: a new 3D scene (with a light), or a light in the selected "
                                                                 "scene. Models: drop an FBX, glTF or OBJ onto a layer or a scene"));
    scene->setObjectName(QStringLiteral("layer3dButton"));
    auto *remove = barButton(QStringLiteral("−"), QStringLiteral("Delete the selected layers (Del)"));
    auto *dup = barButton(QStringLiteral("⧉"), QStringLiteral("Duplicate Layer (Ctrl+D)"));
    (void)title;
    bar->setSpacing(2);
    for (auto *b : {add, remove, group, scene, dup}) bar->addWidget(b);
    bar->addStretch();
    v->addLayout(bar);

    auto *grid = new LayerGrid(0, ColCount);
    grid->rows = &m_rows;
    grid->setAcceptDrops(true);
    grid->viewport()->setAcceptDrops(true);
    grid->setDragDropMode(QAbstractItemView::DragDrop);
    grid->setDragEnabled(false); // drags are started by the grid itself
    grid->onDrop = [this](int row, const QStringList &p) {
        if (!p.isEmpty()) emit filesDropped(row, p);
    };
    grid->onMove = [this](const QList<int> &rows, int before, int parent) { emit moveRequested(rows, before, parent); };
    grid->onCollapse = [this](int row) { emit collapseToggled(row); };
    grid->onRename = [this](int row) { startRename(row); };
    m_table = grid;
    m_table->setHorizontalHeaderLabels({QString(), QString(), QString(), QString(), QStringLiteral("Layer"),
                                        QStringLiteral("Blend"), QStringLiteral("Opa."), QStringLiteral("Audio")});
    m_table->setToolTip(QStringLiteral("Top layer is drawn on top · drop a media onto a layer to load it · "
                                       "drag layers onto a group · double-click a name to rename · "
                                       "the viewports, at the top, are windows onto the composition"));
    m_table->horizontalHeaderItem(ColHandle)->setToolTip(QStringLiteral("Drag to reorder"));
    m_table->horizontalHeaderItem(ColVisible)->setToolTip(QStringLiteral("Enable"));
    m_table->horizontalHeaderItem(ColLock)->setToolTip(QStringLiteral("Locked: no edit allowed"));
    m_table->horizontalHeaderItem(ColSolo)->setToolTip(QStringLiteral("Solo: only the soloed layers are drawn"));
    m_table->horizontalHeaderItem(ColBlend)->setToolTip(QStringLiteral("Blend Mode"));
    m_table->horizontalHeaderItem(ColOpacity)->setToolTip(QStringLiteral("Opacity: drag the number sideways, double-click to type"));
    m_table->horizontalHeaderItem(ColAudio)->setToolTip(QStringLiteral("Layer output after Audio FX and volume · left / right"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(44);
    m_table->setItemDelegateForColumn(ColName, new NameDelegate(m_table));
    m_table->setItemDelegateForColumn(ColSolo, new SoloDelegate(m_table));
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setFocusPolicy(Qt::StrongFocus);
    m_table->setIconSize(QSize(16, 16));
    m_table->setMouseTracking(true); // the handles' hand
    auto *h = m_table->horizontalHeader();
    h->setHighlightSections(false);
    // The name takes the room that is left; the others are as wide as what they show, so no header or cell is cut
    h->setStretchLastSection(false);
    h->setSectionResizeMode(ColHandle, QHeaderView::Fixed);
    m_table->setColumnWidth(ColHandle, 16);
    m_table->setColumnHidden(ColHandle, true); // a layer is dragged from anywhere in its row
    h->setSectionResizeMode(ColVisible, QHeaderView::Fixed);
    m_table->setColumnWidth(ColVisible, 28);
    h->setSectionResizeMode(ColLock, QHeaderView::Fixed);
    m_table->setColumnWidth(ColLock, 26);
    h->setSectionResizeMode(ColSolo, QHeaderView::Fixed);
    m_table->setColumnWidth(ColSolo, 24);
    h->setSectionResizeMode(ColName, QHeaderView::Stretch);
    h->setSectionResizeMode(ColOpacity, QHeaderView::Fixed);
    m_table->setColumnWidth(ColOpacity, 62);
    h->setSectionResizeMode(ColBlend, QHeaderView::Fixed);
    m_table->setColumnWidth(ColBlend, 92);
    h->setSectionResizeMode(ColAudio, QHeaderView::Fixed);
    m_table->setColumnWidth(ColAudio, 64);
    h->setMinimumSectionSize(16);
    h->moveSection(h->visualIndex(ColAudio), h->visualIndex(ColName) + 1);
    m_table->setColumnHidden(ColAudio, true);
    h->setStretchLastSection(false);
    v->addWidget(m_table, 1);

    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row, int, int prev, int) {
        if (!m_updating && row != prev) emit currentRowChanged(row);
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        if (m_updating) return;
        // The current row deselected (Cmd / Ctrl + click): another selected row becomes the current one, or none —
        // the inspector and the mapping view follow, not a row left highlighted nowhere
        const int current = m_table->currentRow();
        if (current >= 0 && !m_table->selectionModel()->isRowSelected(current, QModelIndex())) {
            const QModelIndexList selected = m_table->selectionModel()->selectedRows();
            const int next = selected.isEmpty() ? -1 : selected.front().row();
            m_updating = true;
            m_table->selectionModel()->setCurrentIndex(next < 0 ? QModelIndex() : m_table->model()->index(next, ColName),
                                                       QItemSelectionModel::NoUpdate);
            m_updating = false;
            emit currentRowChanged(next);
        }
        emit selectionChanged();
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_updating || it->column() != ColVisible) return;
        emit visibilityToggled(it->row(), it->checkState() == Qt::Checked);
    });
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (row == m_table->currentRow() && m_table->selectionModel()->isRowSelected(row, QModelIndex()))
            emit rowClicked(row);
        if (col == ColLock) emit lockToggled(row);
        if (col == ColBlend && row >= 0 && row < int(m_rows.size())) {
            const Row &r = m_rows[size_t(row)];
            if (r.noPicture || r.viewport || r.locked || r.lockedByGroup) return;
            QMenu menu(m_table);
            for (BlendMode m : kBlendModes) {
                QAction *a = menu.addAction(blendModeName(m));
                a->setCheckable(true);
                a->setChecked(blendModeName(m) == r.blend);
                connect(a, &QAction::triggered, this, [this, row, m] { emit blendEdited(row, int(m)); });
            }
            menu.exec(m_table->viewport()->mapToGlobal(QPoint(m_table->columnViewportPosition(ColBlend), m_table->rowViewportPosition(row) + m_table->rowHeight(row))));
        }
        if (col == ColSolo && row >= 0 && row < int(m_rows.size()) && !m_rows[size_t(row)].viewport) emit soloToggled(row);
    });
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int row = m_table->rowAt(pos.y());
        // Right-clicking outside the selection acts on the row under the cursor, as everywhere else
        if (row >= 0 && !selectedRows().contains(row)) {
            setCurrentRow(row);
            emit currentRowChanged(row);
        }
        emit contextMenuRequested(row, m_table->viewport()->mapToGlobal(pos));
    });
    auto *f2 = new QShortcut(QKeySequence(Qt::Key_F2), m_table, nullptr, nullptr, Qt::WidgetShortcut);
    connect(f2, &QShortcut::activated, this, [this] { startRename(currentRow()); });
    connect(add, &QToolButton::clicked, this, &LayerTable::addClicked);
    connect(group, &QToolButton::clicked, this, &LayerTable::groupClicked);
    connect(scene, &QToolButton::clicked, this, [this, scene] { emit scene3dClicked(scene); });
    connect(remove, &QToolButton::clicked, this, &LayerTable::removeClicked);
    connect(dup, &QToolButton::clicked, this, &LayerTable::duplicateClicked);
}

QWidget *LayerTable::table() const { return m_table; }

int LayerTable::currentRow() const { return m_table->currentRow(); }

QList<int> LayerTable::selectedRows() const
{
    QList<int> out;
    for (const QModelIndex &i : m_table->selectionModel()->selectedRows()) out << i.row();
    std::sort(out.begin(), out.end());
    return out;
}

void LayerTable::setCurrentRow(int row)
{
    static_cast<LayerGrid *>(m_table)->setAnchor(row);
    m_updating = true;
    if (row < 0) {
        m_table->setCurrentCell(-1, -1);
        m_table->clearSelection();
    } else {
        // Explicit command: a held Ctrl (shortcut) must not toggle the row
        m_table->selectionModel()->setCurrentIndex(m_table->model()->index(row, ColName),
                                                   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    m_updating = false;
}

void LayerTable::startRename(int row)
{
    if (row < 0 || row >= int(m_rows.size())) return;
    const Row &r = m_rows[size_t(row)];
    if (r.locked || r.lockedByGroup) return;
    auto *edit = new RenameEdit(r.name);
    edit->selectAll();
    m_table->setCellWidget(row, ColName, edit);
    edit->setFocus();
    QPointer<RenameEdit> guard(edit);
    // Enter or a click elsewhere validates, Esc cancels. The layer is found again by its id once the editor
    // is gone: the rows may have changed in between.
    const auto finish = [this, guard, id = r.id](bool restoreFocus) {
        if (!guard || guard->property("done").toBool()) return;
        guard->setProperty("done", true);
        const QString name = guard->text().trimmed();
        const bool cancelled = guard->cancelled;
        QTimer::singleShot(0, this, [this, guard, id, name, cancelled, restoreFocus] {
            for (int i = 0; guard && i < m_table->rowCount(); ++i)
                if (m_table->cellWidget(i, ColName) == guard) m_table->removeCellWidget(i, ColName);
            if (restoreFocus) m_table->setFocus();
            if (cancelled || name.isEmpty()) return;
            for (int i = 0; i < int(m_rows.size()); ++i)
                if (m_rows[size_t(i)].id == id) {
                    if (name != m_rows[size_t(i)].name) emit renamed(i, name);
                    return;
                }
        });
    };
    connect(edit, &QLineEdit::editingFinished, this, [finish] { finish(true); });
    // Another layer selected (in the list, the mapping view or the timeline) while renaming: the editor
    // closes and keeps the name typed, without taking the focus from where the selection was made.
    connect(m_table, &QTableWidget::currentCellChanged, edit, [finish, row](int current) {
        if (current != row) finish(false);
    });
}

void LayerTable::updateRow(int r, const Row &row)
{
    auto text = [&](int col, const QString &t, const QString &tip = {}) {
        QTableWidgetItem *it = m_table->item(r, col);
        if (!it) {
            it = new QTableWidgetItem;
            m_table->setItem(r, col, it);
        }
        if (it->text() != t) it->setText(t);
        it->setToolTip(tip.isEmpty() ? t : tip);
        return it;
    };
    // Its handle: dragged, the layer moves (in the list, into a group)
    QTableWidgetItem *handle = m_table->item(r, ColHandle);
    if (!handle) {
        handle = new QTableWidgetItem(QStringLiteral("⠿"));
        handle->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        handle->setTextAlignment(Qt::AlignCenter);
        handle->setForeground(QColor(130, 130, 138));
        handle->setToolTip(QStringLiteral("Drag to reorder, or onto a group"));
        m_table->setItem(r, ColHandle, handle);
    }
    QTableWidgetItem *vis = m_table->item(r, ColVisible);
    if (!vis) {
        vis = new QTableWidgetItem;
        vis->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        m_table->setItem(r, ColVisible, vis);
    }
    vis->setCheckState(row.enabled ? Qt::Checked : Qt::Unchecked);
    vis->setToolTip(QStringLiteral("Enable"));

    QTableWidgetItem *lock = m_table->item(r, ColLock);
    if (!lock) {
        lock = new QTableWidgetItem;
        lock->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(r, ColLock, lock);
    }
    const int lockState = row.locked ? 2 : row.lockedByGroup ? 1 : 0;
    if (lock->data(Qt::UserRole).toInt() != lockState || lock->icon().isNull()) {
        lock->setIcon(padlockIcon(row.locked, row.lockedByGroup));
        lock->setData(Qt::UserRole, lockState);
    }
    lock->setToolTip(row.locked ? QStringLiteral("Locked — click to unlock")
                     : row.lockedByGroup ? QStringLiteral("Locked by its group")
                                         : QStringLiteral("Click to lock (no edit allowed)"));

    // Solo: an S, lit when soloed (not for the viewports)
    QTableWidgetItem *solo = m_table->item(r, ColSolo);
    if (!solo) {
        solo = new QTableWidgetItem;
        solo->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        solo->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(r, ColSolo, solo);
    }
    solo->setText(row.viewport ? QString() : QStringLiteral("S"));
    QFont sf = solo->font();
    sf.setBold(true);
    solo->setFont(sf);
    solo->setForeground(row.solo ? QColor(255, 214, 90) : QColor(95, 95, 102));
    solo->setToolTip(row.viewport ? QString()
                     : row.solo   ? QStringLiteral("Soloed — click to show every layer again")
                                  : QStringLiteral("Solo: draw only this layer (and the other soloed ones)"));

    // The name cell is painted by its delegate: depth, fold arrow, picture, name, then what it is and how it plays
    QTableWidgetItem *name = text(ColName, row.name, row.name + (row.source.isEmpty() ? QString() : QStringLiteral("\n") + row.source));
    QStringList meta;
    if (row.effectCount) meta << QStringLiteral("%1 FX%2").arg(row.effectCount).arg(row.effectsOn ? QString() : QStringLiteral(" (off)"));
    if (!row.playback.isEmpty() && !row.viewport) meta << QString(row.playback).replace(QLatin1Char('\x1f'), QLatin1Char(' '));
    if (!row.source.isEmpty()) meta << row.source;
    name->setData(RoleMeta, meta.join(QStringLiteral(" · ")));
    // The same, in parts: what comes before what runs, what runs (fields), what comes after
    name->setData(RoleMetaHead, row.effectCount ? QStringLiteral("%1 FX%2").arg(row.effectCount).arg(row.effectsOn ? QString() : QStringLiteral(" (off)")) : QString());
    name->setData(RolePlayback, !row.playback.isEmpty() && !row.viewport ? row.playback : QString());
    name->setData(RoleMetaTail, row.source);
    name->setData(RoleDepth, row.depth);
    name->setData(RoleArrow, row.group ? (row.collapsed ? QStringLiteral("▸") : QStringLiteral("▾")) : QString());
    name->setData(RoleTag, row.tag);
    name->setData(RoleThumb, m_thumbs.value(row.id));
    name->setToolTip(row.name + (row.source.isEmpty() ? QString() : QStringLiteral("\n") + row.source) +
                     (row.effectCount ? QStringLiteral("\n") + row.effects.split(QStringLiteral(" › ")).join('\n') : QString()));
    name->setForeground(row.error        ? QColor(255, 110, 95)
                        : row.soloHidden ? QColor(110, 110, 115) // left out by a solo
                        : row.color.isValid() ? row.color
                                              : QColor(230, 230, 233));
    auto *audioCell = m_table->cellWidget(r, ColAudio);
    if (row.hasAudio) {
        if (!audioCell) {
            audioCell = new QWidget;
            audioCell->setAttribute(Qt::WA_TransparentForMouseEvents);
            auto *layout = new QHBoxLayout(audioCell);
            layout->setContentsMargins(5, 0, 5, 0);
            auto *meter = new LevelMeter(QStringLiteral("Layer output"));
            meter->setFixedHeight(meter->sizeHint().height());
            layout->addWidget(meter);
            m_table->setCellWidget(r, ColAudio, audioCell);
        }
    } else if (audioCell) {
        m_table->removeCellWidget(r, ColAudio);
    }
    const QBrush bg = row.viewport ? QBrush(QColor(38, 48, 60)) : row.group ? QBrush(QColor(50, 50, 58)) : QBrush();
    for (int c = 0; c < ColCount; ++c)
        if (QTableWidgetItem *it = m_table->item(r, c)) it->setBackground(bg);
    m_table->setRowHidden(r, row.hidden);

    // Blend Mode: its name in the row; a click opens the menu (flat, like the rest of the list)
    {
        QTableWidgetItem *blend = text(ColBlend, row.noPicture || row.viewport ? QString() : row.blend + QStringLiteral("  ▾"),
                                       QStringLiteral("Blend Mode: click to change"));
        blend->setTextAlignment(Qt::AlignCenter);
        blend->setForeground(row.locked || row.lockedByGroup ? QColor(110, 110, 115) : QColor(190, 190, 198));
    }

    // Opacity: the application's own bar (SliderField, as in the inspector) right in the row; "—" without a picture
    auto *cell = m_table->cellWidget(r, ColOpacity);
    SliderField *slider = cell ? cell->findChild<SliderField *>() : nullptr;
    QLabel *none = cell ? cell->findChild<QLabel *>() : nullptr;
    if (!cell) {
        cell = new QWidget;
        auto *h = new QHBoxLayout(cell);
        h->setContentsMargins(2, 12, 2, 12);
        slider = new SliderField;
        slider->setRange(0, 100);
        slider->setDecimals(0);
        slider->setSuffix(QStringLiteral(" %"));
        slider->setSingleStep(1);
        slider->setSnaps({100});
        slider->setCompact(true);
        slider->setNumberOnly(true);
        slider->setFocusPolicy(Qt::NoFocus);
        none = new QLabel(QStringLiteral("—"));
        none->setAlignment(Qt::AlignCenter);
        h->addWidget(slider, 1);
        h->addWidget(none, 1);
        m_table->setCellWidget(r, ColOpacity, cell);
        connect(slider, &SliderField::valueEdited, this, [this, slider](double v) {
            if (m_updating) return;
            // Find the slider's row (rows may have changed since it was created)
            for (int k = 0; k < m_table->rowCount(); ++k) {
                QWidget *c = m_table->cellWidget(k, ColOpacity);
                if (c && c->isAncestorOf(slider)) {
                    emit opacityEdited(k, v / 100.0);
                    break;
                }
            }
        });
    }
    slider->setVisible(!row.noPicture);
    none->setVisible(row.noPicture);
    slider->setEnabled(!row.locked && !row.lockedByGroup);
}

void LayerTable::setThumbnail(quint64 id, const QImage &image)
{
    const QPixmap pm = image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    m_thumbs.insert(id, pm);
    for (int r = 0; r < int(m_rows.size()); ++r)
        if (m_rows[size_t(r)].id == id)
            if (QTableWidgetItem *it = m_table->item(r, ColName)) it->setData(RoleThumb, pm);
}

void LayerTable::updateRowLevels(int r, const Row &row)
{
    if (row.hasAudio)
        if (auto *cell = m_table->cellWidget(r, ColAudio))
            cell->findChild<LevelMeter *>()->setLevels(row.audioLeft, row.audioRight);
    if (row.noPicture) return;
    auto *slider = m_table->cellWidget(r, ColOpacity)->findChild<SliderField *>();
    const double pct = std::round(row.opacity * 100);
    if (!slider->isDragging() && std::abs(slider->value() - pct) > 1e-9) slider->setValue(pct);
}

// The kinds shown: a bit per category (1 video, 2 3D, 3 audio, 5 ISF)
void LayerTable::setFilter(int mask)
{
    if (mask == m_filter) return;
    m_filter = mask;
    setRows(m_raw);
}

// With a filter, a row shows if its kind is chosen (a group: if something it holds is)
static std::vector<LayerTable::Row> filtered(std::vector<LayerTable::Row> rows, int mask)
{
    if (mask == 0b101110) return rows; // every kind
    std::vector<bool> shown(rows.size(), false);
    for (size_t i = 0; i < rows.size(); ++i)
        if (!rows[i].group && !rows[i].viewport && rows[i].category > 0 && (mask & (1 << rows[i].category))) {
            shown[i] = true;
            // its groups, up the list (a member follows its group, and sits deeper)
            int depth = rows[i].depth;
            for (size_t k = i; k-- > 0 && depth > 0;)
                if (rows[k].group && rows[k].depth < depth) {
                    shown[k] = true;
                    depth = rows[k].depth;
                }
        }
    for (size_t i = 0; i < rows.size(); ++i)
        if (!rows[i].viewport && !shown[i]) rows[i].hidden = true;
    return rows;
}

bool LayerTable::setRows(const std::vector<Row> &input)
{
    m_raw = input;
    const std::vector<Row> rows = filtered(input, m_filter);
    const bool changed = !std::equal(m_rows.begin(), m_rows.end(), rows.begin(), rows.end(),
        [](const Row &a, const Row &b) { return a.id == b.id && a.hidden == b.hidden; });
    auto *grid = static_cast<LayerGrid *>(m_table);
    auto oldId = [this](int row) -> quint64 {
        return row >= 0 && row < int(m_rows.size()) ? m_rows[size_t(row)].id : 0;
    };
    const quint64 currentId = oldId(m_table->currentRow()), anchorId = oldId(grid->anchor());
    QSet<quint64> selected;
    if (changed)
        for (int row : selectedRows()) selected.insert(oldId(row));

    m_updating = true;
    m_table->setColumnHidden(ColAudio, std::none_of(rows.begin(), rows.end(), [](const Row &row) { return row.hasAudio; }));
    if (changed) {
        grid->cancelPendingPress();
        m_table->clearContents();
        m_table->setRowCount(int(rows.size()));
    }
    const auto metadata = [](const Row &row) {
        return std::tie(row.name, row.tag, row.source, row.effects, row.blend, row.color, row.enabled, row.error,
                        row.noPicture, row.hasAudio, row.viewport, row.group, row.collapsed, row.depth, row.locked,
                        row.lockedByGroup, row.effectsOn, row.solo, row.soloHidden, row.effectCount);
    };
    for (int r = 0; r < int(rows.size()); ++r) {
        const auto &row = rows[size_t(r)];
        if (changed || metadata(row) != metadata(m_rows[size_t(r)])) {
            updateRow(r, row);
        } else {
            auto *visible = m_table->item(r, ColVisible);
            const auto state = row.enabled ? Qt::Checked : Qt::Unchecked;
            if (visible->checkState() != state) visible->setCheckState(state); // a refused toggle
            if (row.playback != m_rows[size_t(r)].playback) updateRow(r, row);
        }
        updateRowLevels(r, row);
    }
    m_rows = rows;
    if (changed) {
        int current = -1, anchor = -1, firstSelected = -1;
        QItemSelection restored;
        for (int r = 0; r < int(rows.size()); ++r) {
            const auto &row = rows[size_t(r)];
            if (!row.id || row.hidden || row.viewport) continue;
            if (row.id == anchorId) anchor = r;
            if (!selected.contains(row.id)) continue;
            if (firstSelected < 0) firstSelected = r;
            if (row.id == currentId) current = r;
            restored.select(m_table->model()->index(r, 0), m_table->model()->index(r, m_table->columnCount() - 1));
        }
        if (current < 0) current = firstSelected;
        m_table->selectionModel()->select(restored, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_table->selectionModel()->setCurrentIndex(current < 0 ? QModelIndex() : m_table->model()->index(current, ColName),
                                                   QItemSelectionModel::NoUpdate);
        grid->setAnchor(anchor >= 0 ? anchor : current);
    }
    m_updating = false;
    return changed;
}
