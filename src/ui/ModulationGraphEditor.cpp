#include "ModulationGraphEditor.h"
#include "Engine.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
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
    explicit GraphNode(const ModulationNode &n) : node(n)
    {
        setRect(0, 0, 190, 86 + 28 * n.ports().size());
        setPos(n.position);
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setData(0, n.id);
    }
    QPointF input(int p) const
    {
        return mapToScene({0, 65. + 28 * p});
    }
    QPointF output() const
    {
        return mapToScene({190, rect().height() - 14});
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const QPalette palette = QApplication::palette();
        p->setPen(QPen(isSelected() ? palette.highlight().color() : palette.mid().color(), isSelected() ? 2 : 1));
        p->setBrush(palette.base());
        p->drawRoundedRect(rect(), 6, 6);
        p->setPen(palette.text().color());
        p->drawText(QRectF(10, 5, 170, 22), Qt::AlignLeft | Qt::AlignVCenter, node.label());
        p->drawText(QRectF(10, 29, 170, 22), Qt::AlignLeft | Qt::AlignVCenter,
                    node.condition() ? (value > .5 ? QStringLiteral("TRUE") : QStringLiteral("FALSE"))
                                     : QString::number(value, 'f', 3));
        const auto ports = node.ports();
        for (int i = 0; i < ports.size(); ++i)
        {
            p->setBrush(node.acceptsCondition(i) ? palette.mid() : palette.highlight());
            p->drawEllipse(QPointF(0, 65. + 28 * i), 5, 5);
            p->drawText(QRectF(12, 55. + 28 * i, 160, 22), ports[i]);
        }
        p->setBrush(node.condition() ? palette.mid() : palette.highlight());
        p->drawEllipse(QPointF(190, rect().height() - 14), 5, 5);
        p->drawText(QRectF(10, rect().height() - 27, 166, 22), Qt::AlignRight | Qt::AlignVCenter,
                    isOutput ? QStringLiteral("OUTPUT →") : QStringLiteral("→"));
    }

  protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override
    {
        const QPointF p = e->pos();
        if (p.x() > 174 && p.y() > rect().height() - 30)
        {
            if (portClicked)
                portClicked(node.id, -1);
            e->accept();
            return;
        }
        if (p.x() < 16)
            for (int i = 0; i < node.ports().size(); ++i)
                if (std::abs(p.y() - (65 + 28 * i)) < 13)
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
        bar->addWidget(example);
        bar->addWidget(undo);
        bar->addStretch();
        layout->addLayout(bar);
        status = new QLabel(QStringLiteral(
            "Click an output port, then an input port. Edits apply live. MIDI nodes receive Control Change messages."));
        status->setWordWrap(true);
        layout->addWidget(status);
        auto *row = new QHBoxLayout;
        scene = new QGraphicsScene(this);
        view = new QGraphicsView(scene);
        view->setObjectName("modulationGraphView");
        view->setRenderHint(QPainter::Antialiasing);
        view->setDragMode(QGraphicsView::RubberBandDrag);
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
        connect(zoomIn, &QPushButton::clicked, this, [this] { view->scale(1.25, 1.25); });
        connect(zoomOut, &QPushButton::clicked, this, [this] { view->scale(.8, .8); });
        connect(fit, &QPushButton::clicked, this, [this]
                { view->fitInView(scene->itemsBoundingRect().adjusted(-30, -30, 30, 30), Qt::KeepAspectRatio); });
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
                    view->fitInView(scene->itemsBoundingRect().adjusted(-30, -30, 30, 30), Qt::KeepAspectRatio);
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
        QTimer::singleShot(
            0, this,
            [this] { view->fitInView(scene->itemsBoundingRect().adjusted(-30, -30, 30, 30), Qt::KeepAspectRatio); });
    }

  private:
    Engine *engine;
    quint64 interaction;
    ModulationGraph graph;
    std::function<void(const ModulationGraph &)> apply;
    QComboBox *kind;
    QLabel *status, *result;
    QGraphicsScene *scene;
    QGraphicsView *view;
    QScrollArea *scroll;
    std::map<int, GraphNode *> items;
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
        scene->clear();
        items.clear();
        for (const auto &n : graph.nodes)
        {
            auto *item = new GraphNode(n);
            item->isOutput = n.id == graph.output;
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
                const auto a = items[id]->output(), b = items[n.id]->input(p);
                QPainterPath path(a);
                const qreal bend = std::max(50., std::abs(b.x() - a.x()) * .5);
                path.cubicTo(a + QPointF(bend, 0), b - QPointF(bend, 0), b);
                auto *edge = scene->addPath(path, QPen(QApplication::palette().highlight().color(), 2));
                edge->setZValue(-1);
            }
        scene->setSceneRect(scene->itemsBoundingRect().adjusted(-100, -100, 300, 200));
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
