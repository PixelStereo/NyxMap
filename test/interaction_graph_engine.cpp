#include "Engine.h"
#include "InteractionGraphWorkspace.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <cstdio>
int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    Engine engine;
    QString error;
    if (!engine.initialize(&error)) { std::fprintf(stderr, "%s\n", qPrintable(error)); return 1; }
    engine.setCompositionSize(QSize(64, 32));
    engine.setFadesManual(true);
    int failures = 0;
    auto check = [&](bool ok, const char *why) { if (!ok) { ++failures; std::printf("FAIL: %s\n", why); } };
    const int a = engine.addLayer("Graph A"), b = engine.addLayer("Graph B");
    const auto aid = engine.layerId(a), bid = engine.layerId(b);
    Interaction source = Interaction::made(InteractionSource::Osc);
    source.oscAddress = "/graph";
    Interaction first, second, filter;
    first.layer = aid; first.param = "opacity";
    second.layer = bid; second.param = "opacity";
    auto multiply = InteractionStage::made(InteractionFilter::Math);
    multiply.op = MathOp::Multiply; multiply.a = 2;
    filter.filters = {multiply};
    using Kind = InteractionGraph::Kind;
    InteractionGraph graph;
    graph.nodes = {{1, Kind::Source, source}, {2, Kind::Filter, filter},
                   {3, Kind::Destination, first}, {4, Kind::Destination, second}};
    check(graph.connect(1, 2) && graph.connect(2, 3) && graph.connect(2, 4), "wire graph");
    check(engine.setInteractionGraph(graph, &error), "install graph");
    InteractionEvent event;
    event.source = InteractionSource::Osc; event.address = "/graph"; event.value = .3;
    engine.feedInteraction(event);
    engine.renderFrame();
    check(std::abs(engine.parameterValue(aid, "opacity").toDouble() - .6) < 1e-5, "graph writes first layer");
    check(std::abs(engine.parameterValue(bid, "opacity").toDouble() - .6) < 1e-5, "graph writes second layer");
    check(engine.setParameter(bid, "opacity", .9), "manual edit after graph input");
    engine.renderFrame();
    check(std::abs(engine.parameterValue(bid, "opacity").toDouble() - .9) < 1e-5, "stationary graph leaves a manual edit alone");
    engine.feedInteraction(event);
    engine.renderFrame();
    check(std::abs(engine.parameterValue(bid, "opacity").toDouble() - .6) < 1e-5, "fresh input reclaims its destination");
    QTemporaryDir dir;
    check(engine.saveProject(dir.filePath("graph.nyxmap"), {}, &error), "save graph project");
    engine.newProject();
    check(engine.interactionGraph().nodes.empty(), "new project clears graph");
    check(engine.loadProject(dir.filePath("graph.nyxmap"), nullptr, &error), "load graph project");
    check(engine.interactionGraph().nodes.size() == 4, "graph persists");
    event.value = .2;
    engine.feedInteraction(event);
    engine.renderFrame();
    check(std::abs(engine.parameterValue(aid, "opacity").toDouble() - .4) < 1e-5, "loaded graph executes");
    const auto stored = engine.captureInteractions();
    engine.setInteractionGraph({});
    engine.applyInteractions(stored, 0);
    check(engine.interactionGraph().nodes.size() == 4, "snapshot interaction capture restores graph");
    const auto definition = engine.parameterValue(0, "interaction/0/graph");
    engine.setInteractionGraph({});
    check(engine.setParameter(0, "interaction/0/graph", definition), "graph can be recalled as preset parameter");
    check(engine.interactionGraph().nodes.size() == 4, "preset graph definition restores topology");
    QUndoStack undo;
    {
        InteractionGraphWorkspace workspace(&engine, &undo);
        QWidget host;
        auto *dockLayout = new QVBoxLayout(&host);
        workspace.setDockHost(&host);
        dockLayout->addWidget(&workspace);
        workspace.setDetached(true);
        check(workspace.isDetached() && workspace.isWindow(), "graph and inspector detach together");
        auto *onTop = workspace.findChild<QCheckBox *>("interactionGraphOnTop");
        onTop->setChecked(true);
        check(workspace.windowFlags().testFlag(Qt::WindowStaysOnTopHint), "floating graph supports always on top");
        workspace.close();
        check(!workspace.isDetached() && workspace.parentWidget() == &host, "closing floating graph docks it");
        workspace.setParent(nullptr); // stack workspace must outlive its temporary host safely
        workspace.resize(1000, 600);
        workspace.show();
        app.processEvents();
        if (argc > 1) workspace.grab().save(QString::fromLocal8Bit(argv[1]));
        auto *canvas = static_cast<InteractionGraphView *>(workspace.findChild<QGraphicsView *>("interactionGraphCanvas"));
        check(canvas != nullptr, "graph workspace has a canvas");
        if (canvas) {
            canvas->disconnected(2, 4, 0);
            app.processEvents();
            check(engine.interactionGraph().edges.size() == 2, "disconnect changes engine graph");
            undo.undo(); app.processEvents();
            check(engine.interactionGraph().edges.size() == 3, "undo restores cable");
            canvas->disconnected(2, 4, 0); app.processEvents();
            auto port = [&](quint64 id, bool output) {
                for (auto *item : canvas->scene()->items())
                    if (item->data(0).toULongLong() == id && item->data(1).isValid() && item->data(1).toBool() == output)
                        return canvas->mapFromScene(item->sceneBoundingRect().center());
                return QPoint(-1000, -1000);
            };
            const QPoint from = port(2, true), to = port(4, false);
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), QPointF(canvas->viewport()->mapToGlobal(from)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &press);
            QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(canvas->viewport()->mapToGlobal(to)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &move);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), QPointF(canvas->viewport()->mapToGlobal(to)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &release);
            app.processEvents();
            check(engine.interactionGraph().edges.size() == 3, "mouse drag connects actual canvas ports");
            canvas->removed(2); app.processEvents();
            check(engine.interactionGraph().nodes.size() == 3 && engine.interactionGraph().edges.empty(), "remove block removes attached cables");
            undo.undo(); app.processEvents();
            check(engine.interactionGraph().nodes.size() == 4, "undo restores block and cables");
        }
    }
    engine.setInteractionGraph({});
    Interaction original = source;
    original.layer = aid; original.param = "opacity"; original.filters = {multiply}; original.takeover = Takeover::Jump;
    const auto mapping = engine.addInteraction(original);
    const auto cid = engine.layerId(engine.addLayer("Graph C"));
    {
        InteractionGraphWorkspace workspace(&engine, &undo);
        workspace.addBranch(mapping, false);
        workspace.addBranch(mapping, true);
        auto routes = engine.interactionGraph();
        int sources = 0, destinations = 0;
        for (auto &node : routes.nodes) {
            if (node.kind == Kind::Source) ++sources;
            if (node.kind == Kind::Destination) {
                node.settings.layer = destinations++ ? cid : bid;
                node.settings.param = "opacity";
                node.settings.takeover = Takeover::Jump;
            }
            if (node.kind == Kind::Filter) node.settings.filters.front().a = .5;
        }
        check(sources == 1 && destinations == 2, "both routes share the same mapping output");
        check(engine.setInteractionGraph(routes), "assign branch destinations");
        event.value = .35;
        engine.feedInteraction(event); engine.renderFrame();
        check(std::abs(engine.parameterValue(aid, "opacity").toDouble() - .7) < 1e-5, "original destination remains active");
        check(std::abs(engine.parameterValue(bid, "opacity").toDouble() - .7) < 1e-5, "direct branch uses original filtered value in same frame");
        check(std::abs(engine.parameterValue(cid, "opacity").toDouble() - .35) < 1e-5, "extra filter affects only its own destination");
    }
    {
        InteractionGraphWorkspace workspace(&engine, &undo);
        auto graph = engine.interactionGraph();
        quint64 shared = 0;
        for (const auto &node : graph.nodes) if (node.kind == Kind::Filter) { shared = node.id; break; }
        const auto count = graph.nodes.size();
        const auto child = workspace.addFilterAfter(shared);
        check(child != 0 && engine.interactionGraph().nodes.size() == count + 1, "append a filter without creating a destination");
        graph = engine.interactionGraph();
        check(std::any_of(graph.edges.begin(), graph.edges.end(), [=](const auto &edge) { return edge.from == shared && edge.to == child; }), "shared filter can feed another filter");
        undo.undo(); app.processEvents();
        check(engine.interactionGraph().nodes.size() == count, "branch filter creation is undoable");
    }
    engine.shutdown();
    return failures ? 1 : 0;
}
