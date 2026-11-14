#include "Engine.h"
#include "OscCodec.h"
#include "InteractionOutput.h"
#include "SequenceFeedback.h"
#include <atomic>
#include <QThread>
#include <QNetworkDatagram>
#include <QUdpSocket>
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
    const auto aid = engine.layerId(engine.addLayer("Graph A"));
    const auto bid = engine.layerId(engine.addLayer("Graph B"));
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
    Interaction original = source;
    original.layer = aid;
    original.param = "opacity";
    original.sharedFilters = {multiply};
    original.filters = {multiply};
    original.takeover = Takeover::Jump;
    const auto mapping = engine.addInteraction(original);
    Interaction tap;
    tap.source = InteractionSource::Internal;
    tap.sourceParam = "shared";
    second.filters = {multiply};
    second.filters.front().a = .5;
    second.takeover = Takeover::Jump;
    InteractionGraph targets;
    targets.nodes = {{1, Kind::Source, tap}, {2, Kind::Destination, second}};
    targets.nodes.front().mapping = mapping;
    check(targets.connect(1, 2) && engine.setInteractionGraph(targets), "install a second target");
    event.value = .2;
    engine.feedInteraction(event);
    engine.renderFrame();
    check(std::abs(engine.parameterValue(aid, "opacity").toDouble() - .8) < 1e-5, "shared filters run before target 1 filters");
    check(std::abs(engine.parameterValue(bid, "opacity").toDouble() - .2) < 1e-5, "target 2 receives the shared signal and its own filters");
    check(engine.saveProject(dir.filePath("targets.nyxmap"), {}, &error), "save shared and target filters");
    engine.newProject();
    check(engine.loadProject(dir.filePath("targets.nyxmap"), nullptr, &error), "reload targets");
    event.value = .15;
    engine.feedInteraction(event);
    engine.renderFrame();
    check(std::abs(engine.parameterValue(aid, "opacity").toDouble() - .6) < 1e-5 &&
          std::abs(engine.parameterValue(bid, "opacity").toDouble() - .15) < 1e-5, "both chains survive reload");
    QUdpSocket receiver;
    check(receiver.bind(QHostAddress(QHostAddress::LocalHost), 0), "bind OSC test receiver");
    ShowControl button;
    button.name = "Fire OSC";
    button.kind = ShowControl::Kind::Button;
    const auto buttonId = engine.addControl(button);
    Interaction action = Interaction::made(InteractionSource::Internal);
    action.mode = InteractionMode::Action;
    action.sourceParam = QStringLiteral("control/") + engine.control(buttonId)->key;
    action.conditions = {InteractionCondition{}};
    InteractionConsequence message;
    message.oscDestination = QStringLiteral("127.0.0.1:%1").arg(receiver.localPort());
    message.oscAddress = QStringLiteral("/performance/go");
    message.oscArguments = {7, QStringLiteral("cue"), true, QStringLiteral("$value")};
    action.onTrue = {message};
    engine.addInteraction(action);
    check(engine.saveProject(dir.filePath("control-osc.nyxmap"), {}, &error), "save OSC action");
    engine.newProject();
    check(engine.loadProject(dir.filePath("control-osc.nyxmap"), nullptr, &error), "reload OSC action");
    engine.renderFrame();
    app.processEvents();
    check(!receiver.hasPendingDatagrams(), "loading an OSC action is silent");
    check(engine.setControlValue(buttonId, 1) && engine.setControlValue(buttonId, 0), "quick control press and release");
    engine.renderFrame();
    app.processEvents();
    std::vector<osc::Message> messages;
    check((receiver.hasPendingDatagrams() || receiver.waitForReadyRead(2000)) &&
          osc::decode(receiver.receiveDatagram().data(), messages) && messages.size() == 1 &&
          messages.front().address == "/performance/go" && messages.front().types == "isTf" &&
          messages.front().args.value(0).toInt() == 7 && messages.front().args.value(1).toString() == "cue" &&
          messages.front().args.value(3).toFloat() == 1.f, "OSC sender receives the saved custom packet on press");
    check(!receiver.hasPendingDatagrams(), "release does not send a second packet");
    const auto mappingFor = [&](const InteractionOutput &output) {
        auto route = Interaction::made(InteractionSource::Osc);
        route.oscAddress = "/test/output";
        route.param = output.path();
        return route;
    };
    FixturePatch patch = engine.fixturePatch();
    DmxOutput dmx;
    dmx.id = 42;
    dmx.name = "Test DMX";
    patch.outputs.push_back(dmx);
    engine.setFixturePatch(patch);
    const auto midi = engine.addMidiOutput("Test MIDI");
    std::atomic<int> cc{-1};
    engine.timecodeRouter().setTestSink([&](quint64 id, const std::vector<uint8_t> &bytes) {
        if (id == midi && bytes.size() == 3 && bytes[0] == 0xb1 && bytes[1] == 10) cc = bytes[2];
    });
    engine.addInteraction(mappingFor({"osc", message.oscDestination, "/mapping/out", {QStringLiteral("$value")}}));
    engine.addInteraction(mappingFor({"dmx", "42", {}, {}, 9, 2}));
    engine.addInteraction(mappingFor({"midi", QString::number(midi), {}, {}, 2, 10}));
    event.address = "/test/output";
    event.value = .5;
    engine.feedInteraction(event);
    engine.renderFrame();
    app.processEvents();
    messages.clear();
    check((receiver.hasPendingDatagrams() || receiver.waitForReadyRead(2000)) &&
          osc::decode(receiver.receiveDatagram().data(), messages) && messages.size() == 1 &&
          messages.front().address == "/mapping/out" && messages.front().args.value(0).toFloat() == .5f,
          "a mapping writes its filtered value to an OSC target");
    const auto universe = engine.dmxUniverse(dmx.id, 2);
    check(universe.size() == 512 && quint8(universe.at(8)) == 128, "a mapping writes to the selected DMX universe and channel");
    for (int i = 0; i < 100 && cc == -1; ++i) QThread::msleep(5);
    check(cc == 64, "a mapping sends MIDI CC to the selected output");
    engine.timecodeRouter().setTestSink({});

    SequenceFeedback feedback(&engine);
    feedback.setTargets({message.oscDestination});
    feedback.setEnabled(true);
    const auto receivedControlFeedback = [&] {
        bool received = false;
        while (receiver.hasPendingDatagrams()) {
            messages.clear();
            if (osc::decode(receiver.receiveDatagram().data(), messages))
                for (const auto &packet : messages)
                    received |= packet.address == QLatin1Char('/') + action.sourceParam;
        }
        return received;
    };
    feedback.sendNow(false);
    check(!receivedControlFeedback(), "an unchanged control sends no feedback");
    engine.setControlValue(buttonId, 1);
    feedback.sendNow(false);
    check(receivedControlFeedback(), "a changed control is sent as OSC feedback");
    feedback.sendNow(false);
    check(!receivedControlFeedback(), "feedback does not resend the control's unchanged value");
    feedback.setEnabled(false);
    engine.shutdown();
    return failures ? 1 : 0;
}
