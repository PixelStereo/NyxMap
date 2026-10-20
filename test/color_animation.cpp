#include "Engine.h"
#include "Commands.h"
#include <QTemporaryDir>
#include <QUndoStack>
#include <algorithm>
#include <cmath>
#include <cstdio>

int runColorAnimationTests(Engine &engine, const QString &root)
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) { if (!ok) { ++failures; std::printf("FAIL color_animation: %s\n", why); } };
    engine.newProject();
    engine.setFadesManual(true);
    engine.setCompositionSize({32, 32});
    engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {32, 32});
    const int li = engine.addLayer("Color source");
    const quint64 layer = engine.layerId(li);
    QString error;
    check(engine.setLayerIsf(li, root + "/../isf/generators/SolidColor.fs", &error), "load color generator");
    auto colors = engine.animatableColors(layer);
    check(std::any_of(colors.begin(), colors.end(), [](const auto &p) { return p.path == "param/color"; }), "shader color is grouped explicitly");
    AnimTrack color; color.layer = layer; color.param = "param/color"; color.color = true;
    AnimKey red{0, 0, 0}, blue{2, 0, 0}; red.rgb = {1, 0, 0}; blue.rgb = {0, 0, 1}; color.keys = {red, blue};
    Animation timeline; timeline.name = "RGB"; timeline.duration = 2; timeline.loop = AnimLoop::Once; timeline.tracks = {color};
    const int ti = engine.addAnimation(timeline);
    const quint64 tid = engine.animation(ti).id;
    engine.controlAnimation(tid, AnimAction::Seek, 1);
    engine.renderFrame();
    const QImage image = engine.grabViewport(engine.mainViewportId());
    check(!image.isNull(), "RGB animation produces pixels");
    if (!image.isNull()) {
        const QColor pixel = image.pixelColor(image.width() / 2, image.height() / 2);
        check(std::abs(pixel.red() - 128) <= 3 && pixel.green() <= 3 && std::abs(pixel.blue() - 128) <= 3,
              "rendered midpoint matches the RGB strip");
    }
    QTemporaryDir files;
    const QString project = files.filePath("rgb.pinol");
    check(engine.saveProject(project, {}, &error), "save typed RGB timeline");
    check(engine.loadProject(project, nullptr, &error), "load typed RGB timeline");
    check(engine.animation(engine.indexOfAnimation(tid)).tracks[0].color, "project loader retains color type");
    engine.controlAnimation(tid, AnimAction::Seek, 1.5);
    std::array<double, 3> rgb;
    check(engine.animColorValue(layer, "param/color", &rgb) && std::abs(rgb[0] - .25) < 1e-4 && std::abs(rgb[2] - .75) < 1e-4,
          "loaded RGB timeline drives its source");
    const int index = engine.indexOfId(layer);
    auto sourceAnim = engine.makeLayerAnim(layer, "param/color/r", -1);
    engine.setLayerAnim(layer, "param/color/r", &sourceAnim);
    auto unaffected = engine.makeLayerAnim(layer, "opacity", -1);
    engine.setLayerAnim(layer, "opacity", &unaffected);
    const int fx = engine.addEffect(index, root + "/../isf/effects/Hue.fs", &error);
    check(fx >= 0, "add surviving effect");
    const QString hue = engine.shaderPrefix(layer, fx) + "param/hue";
    auto fxAnimation = engine.makeLayerAnim(layer, hue, -1);
    engine.setLayerAnim(layer, hue, &fxAnimation);
    {
        QUndoStack undo;
        const auto before = engine.layerJson(index);
        const auto animations = engine.captureAnimationTargets(layer);
        engine.clearLayerSource(index);
        undo.push(new cmd::ReplaceLayer(&engine, index, before, "Eject Media", animations));
        check(!engine.layerAnim(layer, "param/color/r", nullptr) && engine.layerAnim(layer, "opacity", nullptr) &&
              engine.layerAnim(layer, hue, nullptr), "source removal deletes its own animations and preserves layer/FX animations");
        check(engine.animation(engine.indexOfAnimation(tid)).tracks.empty(), "source removal deletes its RGB timeline track");
        undo.undo();
        check(engine.layerAnim(layer, "param/color/r", nullptr) && engine.animation(engine.indexOfAnimation(tid)).tracks.size() == 1,
              "source Undo restores local animation and shared RGB track");
        undo.redo();
        check(!engine.layerAnim(layer, "param/color/r", nullptr) && engine.animation(engine.indexOfAnimation(tid)).tracks.empty(),
              "source Redo removes both animations again");
        undo.undo();
    }
    // Two identical effects: the survivor's address changes, but its animation follows the instance.
    check(engine.addEffect(index, root + "/../isf/effects/Hue.fs", &error) >= 0, "add duplicate effect");
    const QString first = engine.shaderPrefix(layer, 0) + "param/hue";
    const QString second = engine.shaderPrefix(layer, 1) + "param/hue";
    auto secondAnim = engine.makeLayerAnim(layer, second, -1);
    secondAnim.tracks[0].keys = {{0, 77, 0}};
    engine.setLayerAnim(layer, second, &secondAnim);
    Animation fxTimeline; fxTimeline.name = "FX targets";
    AnimTrack a; a.layer = layer; a.param = first; a.keys = {{0, 11, 0}};
    AnimTrack b = a; b.param = second; b.keys[0].v = 77;
    AnimTrack opacity = a; opacity.param = "opacity"; opacity.keys[0].v = .6;
    fxTimeline.tracks = {a, b, opacity};
    const int fti = engine.addAnimation(fxTimeline);
    const quint64 ftid = engine.animation(fti).id;
    engine.controlAnimation(ftid, AnimAction::Seek, .75);
    {
        QUndoStack undo;
        const auto before = engine.effectsJson(index);
        const auto animations = engine.captureAnimationTargets(layer);
        engine.removeEffect(index, 0);
        undo.push(new cmd::SetEffects(&engine, index, before, "Remove FX", animations));
        Animation surviving;
        check(!engine.layerAnim(layer, second, nullptr) && engine.layerAnim(layer, first, &surviving) &&
              surviving.tracks[0].keys[0].v == 77, "duplicate survivor retains its own keys at its new address");
        auto tracks = engine.animation(engine.indexOfAnimation(ftid)).tracks;
        check(tracks.size() == 2 && tracks[0].param == first && tracks[0].keys[0].v == 77 && tracks[1].param == "opacity",
              "removed FX track is deleted, survivor is remapped, unrelated track stays");
        check(engine.layerAnim(layer, "param/color/r", nullptr), "FX removal keeps source animation");
        undo.undo();
        check(engine.layerAnim(layer, first, nullptr) && engine.layerAnim(layer, second, nullptr) &&
              engine.animation(engine.indexOfAnimation(ftid)).tracks.size() == 3, "FX Undo restores both target bindings");
        check(std::abs(engine.animation(engine.indexOfAnimation(ftid)).clock - .75) < 1e-6, "binding Undo does not rewind shared playback");
        undo.redo();
        check(engine.animation(engine.indexOfAnimation(ftid)).tracks.size() == 2, "FX Redo removes the deleted target again");
        undo.undo();
    }
    engine.moveEffect(index, 0, 1);
    check(engine.layerAnim(layer, first, nullptr) && engine.layerAnim(layer, second, nullptr), "reordering duplicate effects retains both animations");
    engine.setEffectsJson(index, {});
    check(!engine.layerAnim(layer, first, nullptr) && !engine.layerAnim(layer, second, nullptr), "replacing FX chain prunes deleted FX animations");
    check(engine.animation(engine.indexOfAnimation(ftid)).tracks.size() == 1 &&
          engine.layerAnim(layer, "opacity", nullptr) && engine.layerAnim(layer, "param/color/r", nullptr),
          "chain replacement preserves unrelated layer/source animations");
    auto pasted = engine.layerJson(index);
    pasted["source"] = QJsonObject{{"type", "text"}};
    check(engine.applyLayerParts(index, pasted, Engine::PartSource), "paste a replacement source");
    check(!engine.layerAnim(layer, "param/color/r", nullptr) && engine.layerAnim(layer, "opacity", nullptr) &&
          engine.animation(engine.indexOfAnimation(tid)).tracks.empty(),
          "pasting a source cannot resurrect animations from the removed source");
    engine.newProject();
    const int currentLayer = engine.addLayer("Current RGB");
    const quint64 currentId = engine.layerId(currentLayer);
    check(engine.setLayerIsf(currentLayer, root + "/../isf/generators/SolidColor.fs", &error), "load source for current RGB transition");
    auto setRgb = [&](const QString &path, std::array<double, 3> value) {
        for (int c = 0; c < 3; ++c) engine.setParameter(currentId, path + '/' + QChar("rgb"[c]), value[c]);
    };
    auto nearRgb = [](const std::array<double, 3> &a, const std::array<double, 3> &b) {
        for (int c = 0; c < 3; ++c) if (std::abs(a[c] - b[c]) > 1e-4) return false;
        return true;
    };
    setRgb("param/color", {.2, .4, .6});
    engine.setParameter(currentId, "param/color/a", .37);
    auto currentAnim = engine.makeLayerAnim(currentId, "param/color", -1);
    currentAnim.loop = AnimLoop::Once;
    currentAnim.tracks[0].keys[0].rgb = {1, 0, 0}; // deliberately different placeholder
    currentAnim.tracks[0].keys[0].isCurrentValue = true;
    currentAnim.tracks[0].keys[1].rgb = {0, 0, 1};
    const int currentTimeline = engine.addAnimation(currentAnim);
    const quint64 currentTimelineId = engine.animation(currentTimeline).id;
    engine.controlAnimation(currentTimelineId, AnimAction::Play);
    check(engine.animColorValue(currentId, "param/color", &rgb) && nearRgb(rgb, {.2, .4, .6}), "RGB current start has no initial jump");
    engine.advanceFades(2);
    check(engine.animColorValue(currentId, "param/color", &rgb) && nearRgb(rgb, {.1, .2, .8}), "RGB transition uses the captured start in every channel");
    Engine::AnimationPlayback sample;
    check(engine.sampleAnimation(currentTimelineId, 0, {}, &sample) && sample.capturedRgb.size() == 1 &&
          nearRgb(sample.capturedRgb[0], {.2, .4, .6}), "UI telemetry exposes the captured start, not the moving output");
    engine.controlAnimation(currentTimelineId, AnimAction::Pause);
    setRgb("param/color", {.9, .1, .2});
    engine.controlAnimation(currentTimelineId, AnimAction::Play);
    check(engine.animColorValue(currentId, "param/color", &rgb) && nearRgb(rgb, {.1, .2, .8}), "pause/resume keeps the original color capture");
    auto changed = engine.animation(currentTimeline);
    changed.tracks[0].keys[1].rgb = {0, 1, 0};
    engine.setAnimation(currentTimeline, changed);
    check(nearRgb(engine.animation(currentTimeline).tracks[0].capturedRgb, {.2, .4, .6}), "editing a running color track preserves its capture");
    setRgb("param/color", {.6, .2, .4});
    engine.controlAnimation(currentTimelineId, AnimAction::Rewind);
    check(engine.animColorValue(currentId, "param/color", &rgb) && nearRgb(rgb, {.6, .2, .4}), "rewind captures a fresh RGB starting color");
    engine.controlAnimation(currentTimelineId, AnimAction::Stop);
    setRgb("param/color", {.8, .6, .2});
    engine.controlAnimation(currentTimelineId, AnimAction::Play);
    check(nearRgb(engine.animation(currentTimeline).tracks[0].capturedRgb, {.8, .6, .2}), "restart captures a fresh full color");
    check(std::abs(engine.parameterValue(currentId, "param/color/a").toDouble() - .37) < 1e-4, "current RGB start preserves Alpha");
    setRgb("add", {.3, .5, .7});
    auto localCurrent = engine.makeLayerAnim(currentId, "add", -1);
    localCurrent.tracks[0].keys[0].isCurrentValue = true;
    localCurrent.tracks[0].keys[1].rgb = {1, 1, 1};
    engine.setLayerAnim(currentId, "add", &localCurrent);
    engine.advanceFades(1);
    Animation capturedLocal;
    check(engine.layerAnim(currentId, "add", &capturedLocal) && nearRgb(capturedLocal.tracks[0].capturedRgb, {.3, .5, .7}),
          "local color animation captures its complete starting value");
    Engine::Snapshot cue; cue.layers = engine.captureLayers();
    const int cueIndex = engine.addSnapshot(cue);
    const auto beforeRecall = engine.captureRecallLayers(cueIndex);
    engine.controlLayerAnim(currentId, "add", AnimAction::Rewind);
    engine.restoreRecallLayers(beforeRecall);
    Animation restoredLocal;
    check(engine.layerAnim(currentId, "add", &restoredLocal) && restoredLocal.clock == capturedLocal.clock &&
          nearRgb(restoredLocal.tracks[0].capturedRgb, capturedLocal.tracks[0].capturedRgb), "snapshot Undo restores RGB capture and clock exactly");
    check(!engine.layerJson(engine.indexOfId(currentId)).value("anims").toArray()[0].toObject().contains("_undo_captured_rgb"),
          "runtime RGB capture is absent from persisted layer JSON");
    check(engine.saveProject(files.filePath("current-rgb.pinol"), {}, &error), "save current RGB mode");
    check(engine.loadProject(files.filePath("current-rgb.pinol"), nullptr, &error), "reload current RGB mode");
    check(engine.animation(engine.indexOfAnimation(currentTimelineId)).tracks[0].keys[0].isCurrentValue &&
          engine.layerAnim(currentId, "add", &restoredLocal) && restoredLocal.tracks[0].keys[0].isCurrentValue,
          "project reload retains current-value mode for global and local RGB tracks");
    engine.newProject();
    return failures;
}
