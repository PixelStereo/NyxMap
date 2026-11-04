#pragma once
#include "InteractionGraph.h"
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QGraphicsTextItem>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <functional>

// The canvas edits topology through callbacks: the owning workspace records undo.
class InteractionGraphView : public QGraphicsView {
public:
    std::function<void(quint64)> selected;
    std::function<void(quint64, quint64, int)> connected;
    std::function<void(quint64)> removed;
    std::function<void(quint64, quint64)> disconnected;
    explicit InteractionGraphView(QWidget *parent = nullptr) : QGraphicsView(parent)
    {
        setScene(new QGraphicsScene(this));
        setRenderHint(QPainter::Antialiasing);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setTransformationAnchor(AnchorUnderMouse);
        setMinimumSize(420, 220);
        setObjectName(QStringLiteral("interactionGraphCanvas"));
    }
    void setGraph(const InteractionGraph &graph)
    {
        m_graph = graph;
        m_from = 0;
        m_preview = nullptr;
        scene()->clear();
        m_points.clear();
        m_inputPoints.clear();
        std::vector<size_t> order;
        graph.compile(&order);
        std::map<quint64, int> depth;
        std::map<int, int> rows;
        for (size_t i : order) {
            const auto &node = graph.nodes[i];
            for (const auto &edge : graph.edges)
                if (edge.to == node.id) depth[node.id] = std::max(depth[node.id], depth[edge.from] + 1);
            const int column = depth[node.id];
            int ports = node.kind == InteractionGraph::Kind::Source ? 0 : 1;
            if (int(node.kind) >= int(InteractionGraph::Kind::Add)) {
                ports = 2;
                if (node.kind != InteractionGraph::Kind::Mix && node.kind != InteractionGraph::Kind::Select)
                    for (const auto &edge : graph.edges) if (edge.to == node.id) ports = std::max(ports, edge.port + 2);
            }
            const int height = std::max(64, 24 + ports * 20);
            QPointF at(column * 230, rows[column]);
            rows[column] += height + 40;
            m_points[node.id] = at;
            auto *box = scene()->addRect(QRectF(at, QSizeF(180, height)), QPen(QColor(120, 155, 180)), QBrush(QColor(42, 45, 52)));
            box->setData(0, QVariant::fromValue(node.id));
            QString title = node.settings.name;
            if (title.isEmpty()) {
                if (node.kind == InteractionGraph::Kind::Source) title = interactionSourceName(node.settings.source);
                else if (node.kind == InteractionGraph::Kind::Destination) title = node.settings.param.isEmpty() ? QStringLiteral("Destination") : node.settings.param;
                else if (node.kind == InteractionGraph::Kind::Filter) title = node.settings.filters.empty() ? QStringLiteral("Filter") : interactionFilterName(node.settings.filters.front().kind);
                else title = QStringList{"", "", "", "Add", "Maximum", "Multiply", "Minimum", "Mix", "Select"}.value(int(node.kind));
            }
            auto *text = scene()->addText(title);
            text->setDefaultTextColor(Qt::white);
            text->setTextWidth(155);
            text->setPos(at + QPointF(12, 12));
            text->setData(0, QVariant::fromValue(node.id));
            for (int port = 0; port < ports; ++port) {
                const auto position = at + QPointF(0, ports == 1 ? 32 : 24 + 20 * port);
                m_inputPoints[{node.id, port}] = position;
                addPort(node.id, position, false, port);
            }
            if (node.kind != InteractionGraph::Kind::Destination) addPort(node.id, at + QPointF(180, 32), true);
        }
        for (const auto &edge : graph.edges) {
            auto *line = scene()->addPath(path(m_points[edge.from] + QPointF(180, 32), m_inputPoints[{edge.to, edge.port}]), QPen(QColor(110, 210, 225), 3));
            line->setZValue(-1);
            line->setData(2, QVariant::fromValue(edge.from));
            line->setData(3, QVariant::fromValue(edge.to));
            line->setFlag(QGraphicsItem::ItemIsSelectable);
        }
        scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-50, -50, 100, 100));
    }
protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        auto *item = itemAt(event->pos());
        if (event->button() == Qt::LeftButton && item && item->data(1).isValid()) {
            m_from = item->data(0).toULongLong();
            m_reverse = !item->data(1).toBool();
            m_port = item->data(4).toInt();
            m_start = item->sceneBoundingRect().center();
            m_preview = scene()->addPath(path(m_start, m_start), QPen(Qt::white, 2));
            event->accept();
            return;
        }
        if (item && item->data(0).toULongLong()) {
            m_selected = item->data(0).toULongLong();
            if (selected) selected(m_selected);
        }
        if (!item || item->data(2).isValid()) m_selected = 0;
        QGraphicsView::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_from && m_preview) {
            m_preview->setPath(path(m_start, mapToScene(event->pos())));
            event->accept();
        } else QGraphicsView::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!m_from) { QGraphicsView::mouseReleaseEvent(event); return; }
        delete m_preview; m_preview = nullptr;
        auto *item = itemAt(event->pos());
        const auto from = m_from;
        m_from = 0;
        if (item && item->data(1).isValid() && item->data(1).toBool() == m_reverse && connected) {
            const auto to = item->data(0).toULongLong();
            connected(m_reverse ? to : from, m_reverse ? from : to, m_reverse ? m_port : item->data(4).toInt());
        }
        event->accept();
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape && m_from) {
            delete m_preview; m_preview = nullptr; m_from = 0; event->accept(); return;
        }
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            auto items = scene()->selectedItems();
            if (!items.empty() && items.front()->data(2).isValid() && disconnected) {
                disconnected(items.front()->data(2).toULongLong(), items.front()->data(3).toULongLong());
            } else if (m_selected && removed) removed(m_selected);
            event->accept(); return;
        }
        QGraphicsView::keyPressEvent(event);
    }
    void wheelEvent(QWheelEvent *event) override
    {
        if (event->modifiers() & Qt::ControlModifier) {
            const double factor = event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
            const double zoom = transform().m11() * factor;
            if (zoom >= .2 && zoom <= 4) scale(factor, factor);
            event->accept();
        } else QGraphicsView::wheelEvent(event);
    }
private:
    static QPainterPath path(QPointF a, QPointF b) {
        QPainterPath p(a);
        const double bend = std::max(40., std::abs(b.x() - a.x()) * .5);
        p.cubicTo(a + QPointF(bend, 0), b - QPointF(bend, 0), b);
        return p;
    }
    void addPort(quint64 id, QPointF point, bool output, int input = 0) {
        auto *port = scene()->addEllipse(QRectF(point - QPointF(7, 7), QSizeF(14, 14)), QPen(Qt::white), QBrush(output ? QColor(100, 220, 155) : QColor(100, 170, 240)));
        port->setZValue(2);
        port->setData(0, QVariant::fromValue(id)); port->setData(1, output); port->setData(4, input);
        port->setToolTip(output ? QStringLiteral("Output: drag to an input") : QStringLiteral("Input %1: drag to an output").arg(input + 1));
    }
    InteractionGraph m_graph;
    std::map<quint64, QPointF> m_points;
    quint64 m_from = 0, m_selected = 0;
    bool m_reverse = false;
    int m_port = 0;
    std::map<std::pair<quint64, int>, QPointF> m_inputPoints;
    QPointF m_start;
    QGraphicsPathItem *m_preview = nullptr;
};
