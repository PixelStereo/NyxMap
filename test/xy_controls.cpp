#include "EngineInternal.h"
#include "Osc.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstdio>

int runXyControlTests(Engine &engine, const QString &root)
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL xy_controls: %s\n", why); }
    };
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-5; };
    engine.newProject();
    engine.setFadesManual(true);
    engine.setCompositionSize({32, 32});
    engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {32, 32});
    const int li = engine.addLayer("XY source");
    const quint64 layer = engine.layerId(li);
    QString error;
    if (!engine.setLayerIsf(li, root + "/../isf/generators/SolidColor.fs", &error)) {
        check(false, "load XY fixture source");
        return failures;
    }
    const int fx = engine.addEffect(li, root + "/../isf/effects/Layer Position.fs", &error);
    if (fx < 0) { check(false, "load reported Layer Position FX"); return failures; }
    const QString point = engine.shaderPrefix(layer, fx) + "param/offset";
    const auto points = engine.animatablePoints(layer);
    check(std::any_of(points.begin(), points.end(), [&](const auto &p) { return p.path == point; }), "ISF point2D is an explicit grouped target");
    check(std::any_of(points.begin(), points.end(), [](const auto &p) { return p.path == "position"; }), "native layer position is grouped too");
    const auto colors = engine.animatableColors(layer);
    check(std::none_of(colors.begin(), colors.end(), [&](const auto &p) { return p.path == point; }), "XY never becomes an RGB target");
    auto read = [&](int axis) { return engine.parameterValue(layer, point + (axis ? "/y" : "/x")).toDouble(); };
    auto set = [&](double x, double y) {
        check(engine.setParameter(layer, point + "/x", x), "set independent X");
        check(engine.setParameter(layer, point + "/y", y), "set independent Y");
    };
    set(.1, .2);
    auto animation = engine.makeLayerAnim(layer, point, -1);
    if (animation.tracks.empty() || !animation.tracks[0].isPoint()) {
        check(false, "create typed XY track");
        return failures;
    }
    auto &track = animation.tracks[0];
    track.axes[0].keys = {{0, 0, 0}, {4, .9, 0}};
    track.axes[1].keys = {{0, 0, 0}, {4, .8, 0}};
    for (auto &axis : track.axes) axis.keys.front().isCurrentValue = true;
    animation.loop = AnimLoop::Once;
    const int ti = engine.addAnimation(animation);
    const quint64 tid = engine.animation(ti).id;
    engine.controlAnimation(tid, AnimAction::Play);
    check(near(read(0), .1) && near(read(1), .2), "XY captures both current values without a jump");
    engine.advanceFades(2);
    check(near(read(0), .5) && near(read(1), .5), "one timeline interpolates both independent curves");
    Engine::AnimationPlayback sample;
    check(engine.sampleAnimation(tid, 0, {}, &sample) && sample.capturedXy.size() == 1 &&
          near(sample.capturedXy[0][0], .1) && near(sample.capturedXy[0][1], .2), "telemetry keeps both captured starts");
    auto changed = engine.animation(ti);
    changed.tracks[0].axes[0].enabled = false;
    engine.setAnimation(ti, changed);
    set(.3, .4);
    engine.controlAnimation(tid, AnimAction::Seek, 3);
    check(near(read(0), .3) && near(read(1), .65), "disable X without stopping the grouped Y curve");
    check(!engine.animation(ti).tracks[0].drivenParams().contains(point + "/x"), "disabled axis is not reported as driven");
    engine.layer(li)->locked = true;
    engine.controlAnimation(tid, AnimAction::Seek, 4);
    check(near(read(0), .3) && near(read(1), .65), "lock rejects grouped writes");
    engine.layer(li)->locked = false;
    engine.controlAnimation(tid, AnimAction::Stop);

    auto wave = engine.makeLayerAnim(layer, point, int(AnimWave::Sine));
    check(wave.tracks[0].isPoint() && near(wave.tracks[0].axes[1].phase, .25), "XY waves start a quarter cycle apart");
    wave.tracks[0].axes[0].center = wave.tracks[0].axes[1].center = .5;
    wave.tracks[0].axes[0].amplitude = wave.tracks[0].axes[1].amplitude = .25;
    engine.setLayerAnim(layer, point, &wave);
    engine.controlLayerAnim(layer, point, AnimAction::Seek, 0);
    check(near(read(0), .5) && near(read(1), .75), "phase offset produces a two-dimensional trajectory");
    Animation local;
    check(engine.layerAnim(layer, point, &local), "read grouped local animation");
    const auto serialized = layerAnimToJson(local);
    const auto decoded = layerAnimFromJson(serialized, layer);
    check(decoded.tracks[0].isPoint() && near(decoded.tracks[0].axes[1].phase, .25) &&
          decoded.tracks[0].axes[1].param == point + "/y", "XY curves and phases round-trip with canonical child addresses");
    engine.setLayerAnim(layer, point, nullptr);

    Interaction mapping = Interaction::made(InteractionSource::Osc);
    mapping.oscAddress = "/xy/pad";
    mapping.takeover = Takeover::Jump;
    engine.configureInteractionTarget(mapping, layer, point);
    if (mapping.xyInputs.size() != 1) { check(false, "create XY mapping"); return failures; }
    check(mapping.xyInputs[0].oscArgument == 1, "one OSC XY message defaults to arguments zero and one");
    mapping.srcMax = mapping.xyInputs[0].srcMax = 1000;
    const quint64 mid = engine.addInteraction(mapping);
    mapping.id = mid;
    OscServer server(&engine);
    server.handleMessage({"/xy/pad", "ff", {250.0, 750.0}});
    engine.renderFrame();
    check(near(read(0), .25) && near(read(1), .75), "raw OSC pair uses independent ranges without RGB byte conversion");
    mapping.xyInputs[0].oscAddress = "/xy/vertical";
    mapping.xyInputs[0].oscArgument = 0;
    mapping.xyInputs[0].dstMin = .2;
    mapping.xyInputs[0].dstMax = .6;
    check(engine.setInteraction(mapping), "edit second input binding");
    server.handleMessage({"/xy/vertical", "f", {500.0}});
    engine.renderFrame();
    check(near(read(1), .4), "Y has its own address and output range");
    set(.2, .9);
    server.handleMessage({"/xy/pad", "f", {600.0}});
    engine.renderFrame();
    check(near(read(0), .6) && near(read(1), .9), "sparse X input leaves Y available to other controls");
    check(engine.oscAddressClaimed("/xy/vertical"), "second OSC input owns its address too");

    // One interaction may combine different protocols.
    mapping.source = InteractionSource::Internal;
    mapping.sourceLayer = layer;
    mapping.sourceParam = "opacity";
    mapping.srcMax = 1;
    check(engine.setInteraction(mapping), "combine Internal X with OSC Y");
    check(engine.setParameter(layer, "opacity", .7), "set internal X source");
    server.handleMessage({"/xy/vertical", "f", {250.0}});
    engine.renderFrame();
    check(near(read(0), .7) && near(read(1), .3), "different source protocols drive one XY target");
    auto state = mapping.stateJson();
    mapping.xyInputs[0].oscAddress = "/xy/new-address";
    mapping.xyInputs[0].master = .1;
    mapping.applyState(state);
    check(mapping.xyInputs[0].oscAddress == "/xy/new-address" && near(mapping.xyInputs[0].master, 1),
          "snapshot state restores Y processing without replacing its input binding");
    check(engine.setInteraction(mapping), "store independent Y settings");
    const auto decodedMapping = Interaction::fromJson(mapping.toJson());
    check(decodedMapping.xyInputs.size() == 1 && decodedMapping.xyInputs[0].oscAddress == "/xy/new-address" &&
          near(decodedMapping.xyInputs[0].dstMax, .6), "full mapping round-trip preserves both sources and ranges");
    mapping.xyInputs[0].filter = InteractionFilter::Damper;
    mapping.xyInputs[0].filterB = .77;
    check(engine.setInteraction(mapping), "give Y its own filter settings");
    const QString yMaster = QStringLiteral("interaction/%1/xy/y/master").arg(mid);
    check(engine.setParameter(0, yMaster, .5), "Y processing settings are presettable parameters");
    check(near(engine.interactions()[0].xyInputs[0].master, .5), "Y preset parameter reaches Y rather than X");
    const auto namespaceBefore = OscNamespace::addressOf(&engine, 0, yMaster);
    check(!namespaceBefore.isEmpty(), "Y settings are exposed over OSC");

    Engine::Snapshot cue;
    cue.name = "XY recall";
    cue.fade = 0;
    cue.layers = engine.captureLayers();
    cue.interactions = engine.captureInteractions();
    const int cueIndex = engine.addSnapshot(cue);
    engine.setInteractions({});
    engine.recallSnapshot(cueIndex);
    const auto recalled = engine.interactions();
    check(recalled.size() == 1 && recalled[0].xyInputs.size() == 1 &&
          recalled[0].xyInputs[0].oscAddress == "/xy/new-address" && near(recalled[0].xyInputs[0].master, .5) &&
          recalled[0].xyInputs[0].filter == InteractionFilter::Damper && near(recalled[0].xyInputs[0].filterB, .77),
          "snapshot recreates both input definitions and their independent processing settings");

    engine.setLayerAnim(layer, point, &wave);
    QTemporaryDir files;
    check(engine.saveProject(files.filePath("xy.pinol"), {}, &error), "save XY timelines and local animation");
    check(engine.loadProject(files.filePath("xy.pinol"), nullptr, &error), "reload XY project");
    check(engine.interactions().size() == 1 && engine.interactions()[0].xyInputs.size() == 1 &&
          near(engine.interactions()[0].xyInputs[0].master, .5), "project loader restores the second input and its settings");
    engine.setInteractions({});
    check(engine.layerAnim(layer, point, &local) && local.tracks[0].isPoint() && near(local.tracks[0].axes[1].phase, .25),
          "project loader restores grouped local oscillators");
    const int restoredTimeline = engine.indexOfAnimation(tid);
    check(restoredTimeline >= 0 && engine.animation(restoredTimeline).tracks[0].isPoint(), "project loader restores grouped timeline curves");
    engine.removeEffect(engine.indexOfId(layer), fx);
    check(!engine.layerAnim(layer, point, nullptr) && restoredTimeline >= 0 && engine.animation(restoredTimeline).tracks.empty(),
          "removing an FX removes its grouped local and timeline animations");
    engine.newProject();
    return failures;
}
