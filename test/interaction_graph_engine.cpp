#include "Engine.h"
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
    engine.shutdown();
    return failures ? 1 : 0;
}
