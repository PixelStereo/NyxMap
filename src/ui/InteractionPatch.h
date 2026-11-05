#pragma once
// The interactions seen as nodes: what they listen to on the left, what they shape in the middle, what they drive on the right,
// and the cables between. Another view of the same list: nothing of its own is saved.
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <vector>

class InteractionPatch : public QWidget
{
public:
    struct Link {
        quint64 id = 0;
        QString source, chain, target; // what is written on its nodes
        bool enabled = true, action = false;
        bool envelope = false;
        double attack = 0, decay = 0, sustain = 0, release = 0; // its Envelope filter's, to draw
    };
    std::function<void(quint64)> onSelect, onToggle, onRemove;
    std::function<void(quint64, const QPoint &)> onMenu;                 // a right-click on a link's node
    std::function<void(quint64 from, quint64 to)> onConnect;             // its source cabled to another link's target
    std::function<void(quint64 from, const QPoint &)> onConnectElsewhere; // its source's cable dropped on nothing

    explicit InteractionPatch(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("interactionPatch"));
        setFocusPolicy(Qt::ClickFocus);
        setMouseTracking(true);
        setToolTip(QStringLiteral("Click the middle: its settings below · double-click: on / off · Del: remove\n"
                                  "Drag from a source to a target: a new cable · onto nothing: choose what it drives"));
    }
    void setLinks(std::vector<Link> links, quint64 selected)
    {
        // By source: cables that do not cross for nothing
        std::map<QString, int> first;
        for (size_t k = 0; k < links.size(); ++k) first.try_emplace(links[k].source, int(k));
        std::stable_sort(links.begin(), links.end(), [&](const Link &a, const Link &b) { return first[a.source] < first[b.source]; });
        m_links = std::move(links);
        m_selected = selected;
        layoutNodes();
        update();
    }
    void setLevels(const std::map<quint64, double> &levels)
    {
        if (m_levels == levels) return;
        m_levels = levels;
        if (isVisible()) update();
    }
    const std::vector<Link> &links() const { return m_links; }
    int sourceCount() const { return int(m_sources.size()); }
    int targetCount() const { return int(m_targets.size()); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(22, 22, 26));
        if (m_links.empty()) {
            p.setPen(QColor(130, 130, 136));
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No interaction here yet.\nRight-click a value › Animate › Add Envelope"));
            return;
        }
        const auto cable = [&](const QPointF &a, const QPointF &b, const QColor &c, double width) {
            QPainterPath path(a);
            const double bend = std::max(30.0, (b.x() - a.x()) * 0.5);
            path.cubicTo(a.x() + bend, a.y(), b.x() - bend, b.y(), b.x(), b.y());
            p.setPen(QPen(c, width));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        };
        for (size_t k = 0; k < m_links.size(); ++k) {
            const Link &l = m_links[k];
            const auto found = m_levels.find(l.id);
            const double level = found == m_levels.end() ? 0 : std::clamp(found->second, 0.0, 1.0);
            const QColor c = l.enabled ? QColor(120, 225, 235, 90 + int(165 * level)) : QColor(110, 110, 116, 80);
            const QRectF mid = middleRect(int(k)), from = sourceRect(l.source), to = targetRect(l.target);
            cable(QPointF(from.right(), from.center().y()), QPointF(mid.left(), mid.center().y()), c, 1.2 + 2 * level);
            cable(QPointF(mid.right(), mid.center().y()), QPointF(to.left(), to.center().y()), c, 1.2 + 2 * level);
        }
        const auto node = [&](const QRectF &r, const QString &text, const QColor &edge, bool on, bool chosen) {
            p.setPen(QPen(chosen ? QColor(255, 255, 255) : edge, chosen ? 2 : 1));
            p.setBrush(on ? QColor(44, 44, 50) : QColor(32, 32, 35));
            p.drawRoundedRect(r, 5, 5);
            p.setPen(on ? QColor(230, 230, 233) : QColor(120, 120, 124));
            p.drawText(r.adjusted(7, 0, -7, 0), Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(text, Qt::ElideMiddle, int(r.width()) - 14));
        };
        for (const auto &[text, row] : m_sources) node(sourceRect(text), text, QColor(125, 225, 130), true, false);
        for (const auto &[text, row] : m_targets) node(targetRect(text), text, QColor(255, 196, 90), true, false);
        for (size_t k = 0; k < m_links.size(); ++k) {
            const Link &l = m_links[k];
            const QRectF r = middleRect(int(k));
            node(r, l.envelope ? QString() : l.chain, QColor(195, 150, 255), l.enabled, l.id == m_selected);
            if (l.envelope) { // its shape, small, as the big one
                const QRectF a = r.adjusted(7, 6, -7, -6);
                const double top = a.top(), low = a.bottom(), held = low - std::clamp(l.sustain, 0.0, 1.0) * a.height(), w = a.width() / 3;
                const auto x = [&](double s) { return w * std::sqrt(std::clamp(s / 2, 0.0, 1.0)); };
                QPainterPath path(a.bottomLeft());
                const double xa = a.left() + x(l.attack), xd = xa + x(l.decay);
                path.lineTo(xa, top);
                path.lineTo(xd, held);
                path.lineTo(a.right() - w, held);
                path.lineTo(std::min(a.right(), a.right() - w + x(l.release)), low);
                p.setPen(QPen(l.enabled ? QColor(120, 225, 235) : QColor(110, 110, 116), 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
            }
        }
        p.setPen(QColor(130, 130, 136));
        p.drawText(QRectF(columnX(0), 2, columnWidth(), kTop - 2), Qt::AlignCenter, QStringLiteral("Listens to"));
        p.drawText(QRectF(columnX(1), 2, columnWidth(), kTop - 2), Qt::AlignCenter, QStringLiteral("Shape"));
        p.drawText(QRectF(columnX(2), 2, columnWidth(), kTop - 2), Qt::AlignCenter, QStringLiteral("Drives"));
        if (m_from) cable(sourceRect(linkOf(m_from).source).center(), m_mouse, QColor(255, 255, 255), 2);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const QPointF q = e->position();
        for (size_t k = 0; k < m_links.size(); ++k) {
            if (!middleRect(int(k)).contains(q)) continue;
            m_selected = m_links[k].id;
            if (e->button() == Qt::RightButton && onMenu) onMenu(m_selected, e->globalPosition().toPoint());
            else if (onSelect) onSelect(m_selected);
            update();
            return;
        }
        if (e->button() != Qt::LeftButton) return;
        for (const Link &l : m_links)
            if (sourceRect(l.source).contains(q)) {
                m_from = l.id; // the first of those from this source: all it is, is its source
                m_mouse = q;
                return;
            }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!m_from) return;
        m_mouse = e->position();
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (!m_from) return;
        const quint64 from = m_from;
        m_from = 0;
        update();
        const QPointF q = e->position();
        if (sourceRect(linkOf(from).source).contains(q)) return; // not moved away
        for (const Link &l : m_links)
            if (targetRect(l.target).contains(q)) {
                if (onConnect) onConnect(from, l.id);
                return;
            }
        if (onConnectElsewhere) onConnectElsewhere(from, e->globalPosition().toPoint());
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        for (size_t k = 0; k < m_links.size(); ++k)
            if (middleRect(int(k)).contains(e->position()) && onToggle) {
                onToggle(m_links[k].id);
                return;
            }
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && m_selected && onRemove) {
            onRemove(m_selected);
            return;
        }
        QWidget::keyPressEvent(e);
    }

private:
    static constexpr int kRow = 34, kTop = 22, kHeight = 26, kGap = 26;
    double columnWidth() const { return (width() - 3 * kGap) / 3.0; }
    double columnX(int c) const { return kGap / 2.0 + c * (columnWidth() + kGap); }
    QRectF nodeAt(int column, double row) const { return QRectF(columnX(column), kTop + row * kRow + 2, columnWidth(), kHeight); }
    QRectF middleRect(int k) const { return nodeAt(1, k); }
    QRectF sourceRect(const QString &text) const
    {
        const auto it = m_sources.find(text);
        return nodeAt(0, it == m_sources.end() ? 0 : it->second);
    }
    QRectF targetRect(const QString &text) const
    {
        const auto it = m_targets.find(text);
        return nodeAt(2, it == m_targets.end() ? 0 : it->second);
    }
    Link linkOf(quint64 id) const
    {
        for (const Link &l : m_links)
            if (l.id == id) return l;
        return {};
    }
    // Each source and target in the middle of the links it has, and none over another
    void layoutNodes()
    {
        const auto place = [&](std::map<QString, double> &out, QString Link::*field) {
            std::map<QString, std::pair<double, int>> sum;
            for (size_t k = 0; k < m_links.size(); ++k) {
                auto &s = sum[m_links[k].*field];
                s.first += double(k);
                ++s.second;
            }
            std::vector<std::pair<double, QString>> rows;
            for (const auto &[text, s] : sum) rows.push_back({s.first / s.second, text});
            std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            out.clear();
            double next = 0;
            for (const auto &[row, text] : rows) {
                const double at = std::max(row, next);
                out[text] = at;
                next = at + 1;
            }
        };
        place(m_sources, &Link::source);
        place(m_targets, &Link::target);
        const auto lowest = [](const std::map<QString, double> &m) {
            double low = 0;
            for (const auto &[text, row] : m) low = std::max(low, row);
            return low;
        };
        const double rows = std::max({double(std::max<size_t>(1, m_links.size())), lowest(m_sources) + 1, lowest(m_targets) + 1});
        setMinimumHeight(kTop * 2 + int(kRow * rows));
    }
    std::vector<Link> m_links;
    std::map<QString, double> m_sources, m_targets;
    std::map<quint64, double> m_levels;
    quint64 m_selected = 0, m_from = 0;
    QPointF m_mouse;
};
