#include "Commands.h"
#include "Engine.h"
#include "EngineInternal.h"
#include "PresetEditor.h"
#include <QApplication>
#include <QJsonArray>
#include <QUndoStack>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    Engine engine;
    engine.setFadesManual(true);
    const int row = engine.addLayer("Surface");
    const quint64 layer = engine.layerId(row);
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::printf("FAIL preset_snapshot_integrity: %s\n", why);
        }
    };
    // Undo during a same-grid mesh fade must cancel the remaining job.
    const QJsonObject originalMesh = engine.layer(row)->mapping.meshJson();
    Mapping target = engine.layer(row)->mapping;
    target.offsets[0] = QPointF(.3, .2);
    Engine::Preset preset;
    preset.name = "Warp";
    preset.fade = 2;
    preset.geometry[QString::number(layer)] = target.meshJson();
    const int pi = engine.addPreset(preset);
    {
        QUndoStack undo;
        undo.push(new cmd::RecallPreset(&engine, engine.preset(pi).id));
        engine.advanceFades(.5);
        check(engine.layer(row)->mapping.meshJson() != originalMesh, "mesh fade started");
        undo.undo();
        check(engine.layer(row)->mapping.meshJson() == originalMesh, "undo restores mesh immediately");
        engine.advanceFades(4);
        check(engine.layer(row)->mapping.meshJson() == originalMesh, "undone mesh fade cannot return");
        undo.redo();
        engine.advanceFades(3);
        check(engine.layer(row)->mapping.meshJson() == target.meshJson(), "redo completes the mesh recall");
    }
    // Definition-only edits and delay-only edits must be retained by the Undo command.
    {
        QUndoStack undo;
        auto edit = [&](const auto &fn) { cmd::editPresets(&undo, &engine, "Edit", fn); };
        edit([&](auto &all) { all[pi].values.push_back(engine.presetValue(layer, "opacity")); });
        edit([&](auto &all) { all[pi].values[0].delay = .7; });
        check(engine.preset(pi).values[0].delay == .7, "delay-only edit retained");
        undo.undo();
        check(engine.preset(pi).values[0].delay == 0, "delay-only edit undo");
        undo.redo();
        check(engine.preset(pi).values[0].delay == .7, "delay-only edit redo");
        edit([&](auto &all) {
            Animation a;
            AnimTrack t;
            t.layer = layer;
            t.param = "opacity";
            t.oscillator = true;
            a.tracks = {t};
            all[pi].animations = {a};
        });
        edit([&](auto &all) { all[pi].animations[0].tracks[0].period = 3; });
        undo.undo();
        check(engine.preset(pi).animations[0].tracks[0].period == 1, "animation definition edit undo");
        undo.redo();
        check(engine.preset(pi).animations[0].tracks[0].period == 3, "animation definition edit redo");
    }
    // A snapshot recall adding an animation must remove it again on Undo.
    Engine::Snapshot animated;
    animated.name = "Animated";
    animated.fade = 0;
    Animation animation;
    AnimTrack track;
    track.param = "opacity";
    track.oscillator = true;
    animation.tracks = {track};
    engine.setLayerAnims(layer, {animation});
    animated.layers = engine.captureLayers(true);
    const int animatedIndex = engine.addSnapshot(animated);
    engine.setLayerAnims(layer, {});
    {
        QUndoStack undo;
        undo.push(new cmd::RecallSnapshot(&engine, animatedIndex));
        check(engine.layerAnims(layer).size() == 1, "snapshot installs its animation");
        undo.undo();
        check(engine.layerAnims(layer).empty(), "snapshot Undo restores the exact animation set");
    }
    // Stable identity: deleting an earlier snapshot must not redirect Redo.
    Engine::Snapshot first;
    first.name = "First";
    first.fade = 0;
    first.layers = engine.captureLayers(true);
    const int firstIndex = engine.addSnapshot(first);
    engine.setParameter(layer, "opacity", .2);
    Engine::Snapshot wanted = first;
    wanted.name = "Wanted";
    wanted.layers = engine.captureLayers(true);
    const int wantedIndex = engine.addSnapshot(wanted);
    engine.setParameter(layer, "opacity", .8);
    {
        QUndoStack undo;
        undo.push(new cmd::RecallSnapshot(&engine, wantedIndex));
        undo.undo();
        engine.removeSnapshot(firstIndex);
        undo.redo();
        check(std::abs(engine.parameterValue(layer, "opacity").toDouble() - .2) < 1e-6,
              "Redo follows snapshot identity through list edits");
    }
    // Runtime phase/transport are Undo state, not new project-format fields.
    animation.duration = 1;
    animation.loop = AnimLoop::PingPong;
    engine.setLayerAnims(layer, {animation});
    engine.advanceFades(1.31);
    engine.controlLayerAnim(layer, "opacity", AnimAction::LoopMode, 0, AnimLoop::Loop);
    engine.controlLayerAnim(layer, "opacity", AnimAction::Pause);
    const auto beforeAnimation = engine.layerAnims(layer)[0];
    const double beforeValue = engine.parameterValue(layer, "opacity").toDouble();
    {
        QUndoStack undo;
        undo.push(new cmd::RecallSnapshot(&engine, animatedIndex));
        engine.advanceFades(.4);
        engine.setLayerAnimView(layer, "opacity", true, true);
        undo.undo();
        const auto restored = engine.layerAnims(layer)[0];
        check(restored.state == beforeAnimation.state && restored.clock == beforeAnimation.clock &&
                  restored.oscillatorOffset == beforeAnimation.oscillatorOffset &&
                  restored.reversed == beforeAnimation.reversed,
              "snapshot Undo restores paused transport and oscillator phase");
        check(restored.pinned && restored.folded, "Undo preserves animation card UI changes made after the recall");
        check(std::abs(engine.parameterValue(layer, "opacity").toDouble() - beforeValue) < 1e-6,
              "snapshot Undo restores the displayed animated value");
        check(!engine.layerJson(row).value("anims").toArray()[0].toObject().contains("_undo_clock"),
              "Undo runtime metadata is not persisted in the project");
    }
    {
        Engine::Preset drivers;
        drivers.name = "Drivers";
        drivers.fade = 0;
        drivers.animations = {animation};
        drivers.animations[0].tracks[0].layer = layer;
        const int di = engine.addPreset(drivers);
        const auto before = engine.layerAnims(layer)[0];
        QUndoStack undo;
        undo.push(new cmd::RecallPreset(&engine, engine.preset(di).id));
        engine.advanceFades(.4);
        undo.undo();
        const auto restored = engine.layerAnims(layer)[0];
        check(restored.state == before.state && restored.clock == before.clock &&
                  restored.oscillatorOffset == before.oscillatorOffset && restored.reversed == before.reversed,
              "preset Undo restores exact animation transport");
    }
    const auto serialized = presetToJson(engine.preset(pi));
    check(presetToJson(presetFromJson(serialized)) == serialized,
          "preset serialization preserves geometry, definitions and delay metadata");
    return failures ? 1 : 0;
}
