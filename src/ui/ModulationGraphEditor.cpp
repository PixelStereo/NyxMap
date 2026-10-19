#include "ModulationGraphEditor.h"
#include "Engine.h"
#include "ModulationGraphView.h"
#include "Widgets.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>

namespace
{
class GraphNode : public QGraphicsRectItem
{
  public:
    ModulationNode node;
    double value = 0;
    bool isOutput = false;
    std::function<void(int, int)> portClicked;
    std::function<void(int, QPointF)> moved;
    std::function<void()> positionChanged;
    explicit GraphNode(const ModulationNode &n) : node(n)
    {
        setRect(0, 0, 216, 104 + 28 * n.ports().size());
        setPos(n.position);
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setData(0, n.id);
    }
    QPointF input(int p) const { return mapToScene({0, 82. + 28 * p}); }
    QPointF output() const { return mapToScene({216, rect().height() - 18}); }
    QRectF boundingRect() const override
    {
        // The input/output circles and selection stroke are painted outside the body.
        return rect().adjusted(-8, -3, 8, 3);
    }
    QPainterPath shape() const override
    {
        QPainterPath path;
        path.addRoundedRect(rect(), 7, 7);
        for (int port = 0; port < node.ports().size(); ++port)
            path.addEllipse(QPointF(0, 82. + 28 * port), 7, 7);
        path.addEllipse(QPointF(216, rect().height() - 18), 7, 7);
        return path;
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        p->save();
        const QPalette palette = QApplication::palette();
        const bool dark = palette.window().color().lightness() < 128;
        const QColor body = dark ? palette.window().color().lighter(118) : palette.base().color();
        const QColor header = dark ? body.lighter(115) : body.darker(104);
        const QColor border = dark ? body.lighter(165) : body.darker(125);
        const QColor accent = theme::accent();
        const QColor text = palette.text().color();
        const QColor secondary = dark ? text.darker(125) : text.lighter(125);
        p->setPen(Qt::NoPen);
        p->setBrush(body);
        p->drawRoundedRect(rect(), 7, 7);
        QPainterPath clip;
        clip.addRoundedRect(rect(), 7, 7);
        p->save();
        p->setClipPath(clip);
        p->fillRect(QRectF(0, 0, rect().width(), 30), header);
        p->restore();
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(isSelected() ? accent : border, isSelected() ? 1.8 : 1));
        p->drawRoundedRect(rect(), 7, 7);
        QFont titleFont = p->font();
        titleFont.setWeight(QFont::Medium);
        p->setFont(titleFont);
        p->setPen(text);
        const QFontMetricsF metrics(titleFont);
        p->drawText(QRectF(13, 4, 190, 22), Qt::AlignLeft | Qt::AlignVCenter,
                    metrics.elidedText(node.label(), Qt::ElideRight, 190));
        QFont valueFont = titleFont;
        valueFont.setPointSizeF(titleFont.pointSizeF() + 3);
        p->setFont(valueFont);
        p->drawText(QRectF(13, 38, 190, 25), Qt::AlignLeft | Qt::AlignVCenter,
                    node.condition() ? (value > .5 ? QStringLiteral("TRUE") : QStringLiteral("FALSE"))
                                     : QString::number(value, 'f', 3));
        p->setFont(titleFont);
        const auto ports = node.ports();
        auto port = [&](QPointF center, bool condition)
        {
            p->setPen(QPen(body, 1.5));
            p->setBrush(condition ? secondary : accent);
            if (condition)
            {
                QPolygonF diamond;
                diamond << center + QPointF(0, -6) << center + QPointF(6, 0) << center + QPointF(0, 6)
                        << center + QPointF(-6, 0);
                p->drawPolygon(diamond);
            }
            else
                p->drawEllipse(center, 5.5, 5.5);
        };
        for (int i = 0; i < ports.size(); ++i)
        {
            port(QPointF(0, 82. + 28 * i), node.acceptsCondition(i));
            p->setPen(secondary);
            p->drawText(QRectF(13, 71. + 28 * i, 180, 22), ports[i]);
        }
        port(QPointF(216, rect().height() - 18), node.condition());
        p->setPen(isOutput ? accent : secondary);
        p->drawText(QRectF(13, rect().height() - 29, 188, 22), Qt::AlignRight | Qt::AlignVCenter,
                    isOutput           ? QStringLiteral("Output")
                    : node.condition() ? QStringLiteral("Condition")
                                       : QStringLiteral("Signal"));
        p->restore();
    }

  protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override
    {
        const QVariant result = QGraphicsRectItem::itemChange(change, value);
        if (change == ItemPositionHasChanged && positionChanged)
            positionChanged();
        return result;
    }
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override
    {
        const QPointF p = e->pos();
        if (p.x() > 200 && p.y() > rect().height() - 34)
        {
            if (portClicked)
                portClicked(node.id, -1);
            e->accept();
            return;
        }
        if (p.x() < 16)
            for (int i = 0; i < node.ports().size(); ++i)
                if (std::abs(p.y() - (82 + 28 * i)) < 13)
                {
                    if (portClicked)
                        portClicked(node.id, i);
                    e->accept();
                    return;
                }
        QGraphicsRectItem::mousePressEvent(e);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override
    {
        QGraphicsRectItem::mouseReleaseEvent(e);
        if (pos() != node.position && moved)
            moved(node.id, pos());
    }
};
class Editor : public QDialog
{
  public:
    Editor(Engine *e, quint64 id, const ModulationGraph &g, std::function<void(const ModulationGraph &)> commit,
           QWidget *parent)
        : QDialog(parent), engine(e), interaction(id), graph(g), apply(std::move(commit))
    {
        setWindowTitle(QStringLiteral("Modulation Graph"));
        resize(1200, 800);
        setObjectName(QStringLiteral("modulationGraphEditor"));
        auto *layout = new QVBoxLayout(this);
        auto *bar = new QHBoxLayout;
        kind = new QComboBox;
        kind->setObjectName("graphNodeType");
        for (int t = 0; t <= ModulationNode::Clamp; ++t)
            kind->addItem(ModulationNode::typeName(ModulationNode::Type(t)), t);
        auto *add = new QPushButton(QStringLiteral("Add Node"));
        add->setObjectName("graphAddNode");
        auto *fit = new QPushButton(QStringLiteral("Fit"));
        auto *zoomIn = new QPushButton(QStringLiteral("Zoom In"));
        auto *zoomOut = new QPushButton(QStringLiteral("Zoom Out"));
        auto *example = new QPushButton(QStringLiteral("Load Example"));
        auto *undo = new QPushButton(QStringLiteral("Undo"));
        undo->setObjectName("graphUndo");
        bar->addWidget(kind);
        bar->addWidget(add);
        bar->addWidget(fit);
        bar->addWidget(zoomIn);
        bar->addWidget(zoomOut);
        auto *zoomLevel = new QLabel;
        zoomLevel->setMinimumWidth(48);
        bar->addWidget(zoomLevel);
        bar->addWidget(example);
        bar->addWidget(undo);
        bar->addStretch();
        layout->addLayout(bar);
        status = new QLabel(
            QStringLiteral("Pinch to zoom · two-finger scroll to pan · click an output, then an input to connect."));
        status->setWordWrap(true);
        layout->addWidget(status);
        auto *row = new QHBoxLayout;
        scene = new QGraphicsScene(this);
        view = new ModulationGraphView(scene);
        view->setObjectName("modulationGraphView");
        view->zoomChanged = [zoomLevel](double value)
        { zoomLevel->setText(QStringLiteral("%1%").arg(qRound(value * 100))); };
        row->addWidget(view, 1);
        scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setMinimumWidth(260);
        scroll->setMaximumWidth(310);
        row->addWidget(scroll);
        layout->addLayout(row, 1);
        auto *footer = new QHBoxLayout;
        result = new QLabel(QStringLiteral("Output: — · waiting for frames or disabled"));
        result->setObjectName("graphLiveOutput");
        footer->addWidget(result);
        footer->addStretch();
        auto *close = new QPushButton(QStringLiteral("Close"));
        footer->addWidget(close);
        layout->addLayout(footer);
        connect(close, &QPushButton::clicked, this, &QDialog::accept);
        connect(zoomIn, &QPushButton::clicked, this, [this] { view->zoomAt(view->viewport()->rect().center(), 1.25); });
        connect(zoomOut, &QPushButton::clicked, this, [this] { view->zoomAt(view->viewport()->rect().center(), .8); });
        connect(fit, &QPushButton::clicked, this, [this] { view->fitGraph(scene->itemsBoundingRect()); });
        connect(add, &QPushButton::clicked, this,
                [this]
                {
                    if (graph.nodes.size() >= ModulationGraph::MaxNodes)
                        return;
                    remember();
                    ModulationNode n;
                    n.type = ModulationNode::Type(kind->currentData().toInt());
                    for (const auto &x : graph.nodes)
                        n.id = std::max(n.id, x.id);
                    ++n.id;
                    n.position = view->mapToScene(view->viewport()->rect().center());
                    graph.nodes.push_back(n);
                    selected = n.id;
                    changed(true);
                });
        connect(example, &QPushButton::clicked, this,
                [this]
                {
                    remember();
                    graph = ModulationGraph::example();
                    selected = 3;
                    changed(true);
                    view->fitGraph(scene->itemsBoundingRect());
                });
        connect(undo, &QPushButton::clicked, this,
                [this]
                {
                    if (history.empty())
                        return;
                    graph = history.back();
                    history.pop_back();
                    pending = 0;
                    changed(true);
                });
        connect(scene, &QGraphicsScene::selectionChanged, this,
                [this]
                {
                    if (drawing)
                        return;
                    const auto items = scene->selectedItems();
                    if (items.empty())
                        return;
                    selected = static_cast<GraphNode *>(items.front())->node.id;
                    updateEdges();
                    properties();
                });
        selected = graph.nodes.empty() ? 0 : graph.nodes.front().id;
        draw();
        properties();
        auto *timer = new QTimer(this);
        timer->setInterval(50);
        connect(
            timer, &QTimer::timeout, this,
            [this]
            {
                const auto v = engine->graphNodeValues(interaction);
                if (v.size() != graph.nodes.size())
                    return;
                for (size_t i = 0; i < v.size(); ++i)
                {
                    auto it = items.find(graph.nodes[i].id);
                    if (it != items.end())
                    {
                        it->second->value = v[i];
                        it->second->update();
                    }
                    if (graph.nodes[i].id == graph.output)
                        result->setText(
                            QStringLiteral("Output: %1 · target range applied in Interactions").arg(v[i], 0, 'f', 3));
                }
            });
        timer->start();
        QTimer::singleShot(0, this, [this] { view->fitGraph(scene->itemsBoundingRect()); });
    }

  private:
    Engine *engine;
    quint64 interaction;
    ModulationGraph graph;
    std::function<void(const ModulationGraph &)> apply;
    QComboBox *kind;
    QLabel *status, *result;
    QGraphicsScene *scene;
    ModulationGraphView *view;
    QScrollArea *scroll;
    std::map<int, GraphNode *> items;
    struct Edge
    {
        QGraphicsPathItem *item;
        int source, target, port;
    };
    std::vector<Edge> edges;
    void updateEdges()
    {
        for (const auto &edge : edges)
        {
            const auto *source = items.at(edge.source), *target = items.at(edge.target);
            const auto a = source->output(), b = target->input(edge.port);
            QPainterPath path(a);
            const bool forward = b.x() > a.x();
            bool blocked = false;
            if (forward)
            {
                const qreal bend = std::max(12., (b.x() - a.x()) * .5);
                path.cubicTo(a + QPointF(bend, 0), b - QPointF(bend, 0), b);
                for (const auto &obstacle : items)
                {
                    if (obstacle.first == edge.source || obstacle.first == edge.target)
                        continue;
                    if (path.intersects(obstacle.second->sceneBoundingRect().adjusted(-4, -4, 4, 4)))
                    {
                        blocked = true;
                        break;
                    }
                }
            }
            if (!forward || blocked)
            {
                // Route obstructed links through the clear lanes outside the node bodies.
                qreal top = a.y(), bottom = a.y();
                for (const auto &item : items)
                {
                    top = std::min(top, item.second->sceneBoundingRect().top());
                    bottom = std::max(bottom, item.second->sceneBoundingRect().bottom());
                }
                const qreal y = forward ? top - 28 : bottom + 28;
                const qreal right = a.x() + 16, left = b.x() - 16;
                const std::vector<QPointF> points{a, {right, a.y()}, {right, y}, {left, y}, {left, b.y()}, b};
                path = QPainterPath(a);
                for (size_t i = 1; i + 1 < points.size(); ++i)
                {
                    const QLineF incoming(points[i], points[i - 1]), outgoing(points[i], points[i + 1]);
                    const double r = std::min({10., incoming.length() / 2, outgoing.length() / 2});
                    if (incoming.length() < 1e-6 || outgoing.length() < 1e-6)
                        continue;
                    const auto before = incoming.pointAt(r / incoming.length()),
                               after = outgoing.pointAt(r / outgoing.length());
                    path.lineTo(before);
                    path.quadTo(points[i], after);
                }
                path.lineTo(b);
            }
            QColor color = theme::accent();
            const bool emphasized = source->isSelected() || target->isSelected();
            color.setAlpha(emphasized ? 230 : 145);
            QPen pen(color, emphasized ? 2. : 1.5);
            pen.setCosmetic(true);
            edge.item->setPen(pen);
            edge.item->setPath(path);
        }
    }
    std::vector<ModulationGraph> history;
    int selected = 0, pending = 0;
    bool drawing = false;
    ModulationNode *current()
    {
        for (auto &n : graph.nodes)
            if (n.id == selected)
                return &n;
        return nullptr;
    }
    void remember()
    {
        history.push_back(graph);
        if (history.size() > 100)
            history.erase(history.begin());
    }
    void changed(bool redraw = false)
    {
        QString error;
        if (!graph.validate(&error))
        {
            status->setText(error);
            return;
        }
        apply(graph);
        status->setText(QStringLiteral("Live · %1 nodes · output: %2")
                            .arg(graph.nodes.size())
                            .arg(graph.find(graph.output)->label()));
        if (redraw)
        {
            draw();
            properties();
        }
    }
    void ports(int id, int port)
    {
        if (port < 0)
        {
            pending = id;
            status->setText(QStringLiteral("Connect %1 to an input port.").arg(graph.find(id)->label()));
            return;
        }
        if (!pending)
        {
            selected = id;
            properties();
            return;
        }
        if (!graph.canConnect(pending, id, port))
        {
            status->setText(QStringLiteral("Connection refused: incompatible types or a cycle."));
            pending = 0;
            return;
        }
        remember();
        for (auto &n : graph.nodes)
            if (n.id == id)
                n.inputs[size_t(port)] = pending;
        pending = 0;
        QTimer::singleShot(0, this, [this] { changed(true); });
    }
    void draw()
    {
        drawing = true;
        edges.clear();
        scene->clear();
        items.clear();
        for (const auto &n : graph.nodes)
        {
            auto *item = new GraphNode(n);
            item->isOutput = n.id == graph.output;
            item->positionChanged = [this] { updateEdges(); };
            item->portClicked = [this](int id, int p) { ports(id, p); };
            item->moved = [this](int id, QPointF pos)
            {
                remember();
                for (auto &x : graph.nodes)
                    if (x.id == id)
                        x.position = pos;
                QTimer::singleShot(0, this, [this] { changed(true); });
            };
            items[n.id] = item;
            scene->addItem(item);
            if (n.id == selected)
                item->setSelected(true);
        }
        for (const auto &n : graph.nodes)
            for (int p = 0; p < n.ports().size(); ++p)
            {
                const int id = n.inputs[size_t(p)];
                if (!items.count(id))
                    continue;
                QPen pen(theme::accent(), 1.8);
                pen.setCosmetic(true);
                auto *edge = scene->addPath(QPainterPath(), pen);
                edge->setAcceptedMouseButtons(Qt::NoButton);
                edge->setData(1, id);
                edge->setData(2, n.id);
                edge->setZValue(-1);
                edges.push_back({edge, id, n.id, p});
            }
        updateEdges();
        scene->setSceneRect(scene->itemsBoundingRect().adjusted(-1500, -1500, 1500, 1500));
        drawing = false;
    }
    void properties()
    {
        auto *box = new QWidget;
        auto *form = new QFormLayout(box);
        scroll->setWidget(box);
        auto *n = current();
        if (!n)
        {
            form->addRow(new QLabel(QStringLiteral("Select a node.")));
            return;
        }
        const int id = n->id;
        auto *title = new QLabel(n->label());
        form->addRow(title);
        auto *name = new QLineEdit(n->name);
        name->setObjectName("graphNodeName");
        form->addRow(QStringLiteral("Name"), name);
        connect(name, &QLineEdit::editingFinished, this,
                [this, id, name]
                {
                    remember();
                    for (auto &x : graph.nodes)
                        if (x.id == id)
                            x.name = name->text();
                    changed();
                    items[id]->node.name = name->text();
                    items[id]->update();
                });
        const auto labels = n->ports();
        for (int p = 0; p < labels.size(); ++p)
        {
            auto *source = new QComboBox;
            source->setObjectName(QStringLiteral("graphInput%1").arg(p));
            source->addItem(QStringLiteral("Unconnected (0)"), 0);
            for (const auto &s : graph.nodes)
                if (s.id != id && graph.canConnect(s.id, id, p))
                    source->addItem(s.label(), s.id);
            source->setCurrentIndex(std::max(0, source->findData(n->inputs[size_t(p)])));
            form->addRow(labels[p], source);
            connect(source, qOverload<int>(&QComboBox::activated), this,
                    [this, id, p, source]
                    {
                        remember();
                        for (auto &x : graph.nodes)
                            if (x.id == id)
                                x.inputs[size_t(p)] = source->currentData().toInt();
                        QTimer::singleShot(0, this, [this] { changed(true); });
                    });
        }
        auto number = [&](const QString &label, double ModulationNode::*member, double min, double max,
                          const QString &suffix = QString())
        {
            auto *spin = new QDoubleSpinBox;
            spin->setObjectName("graph" + label.simplified().remove(' '));
            spin->setRange(min, max);
            spin->setDecimals(3);
            spin->setSingleStep(.01);
            spin->setSuffix(suffix);
            spin->setValue(n->*member);
            form->addRow(label, spin);
            connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [this, id, member](double v)
                    {
                        remember();
                        for (auto &x : graph.nodes)
                            if (x.id == id)
                                x.*member = v;
                        changed();
                    });
        };
        auto text = [&](const QString &label, QString ModulationNode::*member)
        {
            auto *line = new QLineEdit(n->*member);
            form->addRow(label, line);
            connect(line, &QLineEdit::editingFinished, this,
                    [this, id, member, line]
                    {
                        remember();
                        for (auto &x : graph.nodes)
                            if (x.id == id)
                                x.*member = line->text();
                        changed();
                    });
        };
        auto integer = [&](const QString &label, int ModulationNode::*member, int max)
        {
            auto *spin = new QSpinBox;
            spin->setRange(0, max);
            spin->setValue(n->*member);
            form->addRow(label, spin);
            connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                    [this, id, member](int v)
                    {
                        remember();
                        for (auto &x : graph.nodes)
                            if (x.id == id)
                                x.*member = v;
                        changed();
                    });
        };
        if (n->type == ModulationNode::Osc || n->type == ModulationNode::Midi || n->type == ModulationNode::Constant)
            number(n->type == ModulationNode::Constant ? QStringLiteral("Value") : QStringLiteral("Initial value"),
                   &ModulationNode::value, -1e6, 1e6);
        if (n->type == ModulationNode::Osc)
        {
            text(QStringLiteral("OSC address"), &ModulationNode::address);
            integer(QStringLiteral("Argument"), &ModulationNode::argument, 1024);
        }
        if (n->type == ModulationNode::Midi)
        {
            text(QStringLiteral("Input port (empty: any)"), &ModulationNode::port);
            integer(QStringLiteral("Channel (0: any)"), &ModulationNode::channel, 16);
            integer(QStringLiteral("CC number"), &ModulationNode::number, 127);
        }
        if (n->type == ModulationNode::Audio)
        {
            auto *src = new QComboBox;
            src->addItems({QStringLiteral("Program mix"), QStringLiteral("Live input")});
            src->setCurrentIndex(n->audioInput ? 1 : 0);
            form->addRow(QStringLiteral("Audio source"), src);
            connect(src, qOverload<int>(&QComboBox::activated), this,
                    [this, id](int v)
                    {
                        remember();
                        for (auto &x : graph.nodes)
                            if (x.id == id)
                                x.audioInput = v == 1;
                        changed();
                    });
            auto *measure = new QComboBox;
            measure->addItems(
                {QStringLiteral("Frequency band"), QStringLiteral("Overall level"), QStringLiteral("Onset")});
            measure->setCurrentIndex(n->audioFeature);
            form->addRow(QStringLiteral("Measure"), measure);
            connect(measure, qOverload<int>(&QComboBox::activated), this,
                    [this, id](int v)
                    {
                        remember();
                        for (auto &x : graph.nodes)
                            if (x.id == id)
                                x.audioFeature = v;
                        changed();
                    });
            number(QStringLiteral("Low"), &ModulationNode::low, 20, 20000, QStringLiteral(" Hz"));
            number(QStringLiteral("High"), &ModulationNode::high, 20, 20000, QStringLiteral(" Hz"));
        }
        if (n->type == ModulationNode::Sine)
        {
            number(QStringLiteral("Frequency"), &ModulationNode::frequency, 0, 100, QStringLiteral(" Hz"));
            number(QStringLiteral("Amplitude"), &ModulationNode::amplitude, -100, 100);
            number(QStringLiteral("Offset"), &ModulationNode::offset, -100, 100);
        }
        if (n->type == ModulationNode::Below)
        {
            number(QStringLiteral("Threshold"), &ModulationNode::threshold, -1e6, 1e6);
            number(QStringLiteral("Hysteresis"), &ModulationNode::hysteresis, 0, 1e6);
        }
        if (n->type == ModulationNode::Smooth)
            number(QStringLiteral("Time constant"), &ModulationNode::seconds, 0, 3600, QStringLiteral(" s"));
        if (!n->condition())
        {
            auto *out = new QPushButton(n->id == graph.output ? QStringLiteral("Current Output")
                                                              : QStringLiteral("Use as Output"));
            out->setEnabled(n->id != graph.output);
            form->addRow(out);
            connect(out, &QPushButton::clicked, this,
                    [this, id]
                    {
                        remember();
                        graph.output = id;
                        changed(true);
                    });
        }
        auto *remove = new QPushButton(QStringLiteral("Delete Node"));
        remove->setObjectName("graphDeleteNode");
        remove->setEnabled(n->id != graph.output);
        form->addRow(remove);
        connect(remove, &QPushButton::clicked, this,
                [this, id]
                {
                    remember();
                    graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(),
                                                     [id](const auto &x) { return x.id == id; }),
                                      graph.nodes.end());
                    for (auto &x : graph.nodes)
                        for (auto &in : x.inputs)
                            if (in == id)
                                in = 0;
                    selected = graph.output;
                    changed(true);
                });
    }
};
} // namespace
void editModulationGraph(Engine *engine, quint64 interaction, const ModulationGraph &graph,
                         const std::function<void(const ModulationGraph &)> &commit, QWidget *parent)
{
    Editor dialog(engine, interaction, graph, commit, parent);
    dialog.exec();
}
