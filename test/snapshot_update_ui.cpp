#include "Engine.h"
#include "PresetEditor.h"
#include <QApplication>
#include <QMenu>
#include <QUndoStack>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Engine engine;
    engine.setFadesManual(true);
    const int row = engine.addLayer("Surface");
    const auto layer = engine.layerId(row);
    QUndoStack undo;
    PresetEditor editor(&engine, &undo);
    Engine::Snapshot snapshot;
    snapshot.name = "Cue A";
    snapshot.layers = engine.captureLayers();
    snapshot.composition = engine.captureComposition();
    const int index = engine.addSnapshot(snapshot);
    const auto target = engine.snapshot(index).id;
    int failures = 0;
    auto check = [&](bool ok, const char *why) { if (!ok) { ++failures; std::printf("FAIL snapshot_update_ui: %s\n", why); } };
    for (const auto &path : {"audio/level","audio/bass","audio/mid","audio/high","audio/onset"}) {
        ParamInfo info;
        check(engine.parameterInfo(0,path,&info) && info.access==ParamInfo::Read, "audio analysis is a readable Internal interaction source");
        check(!engine.setParameter(0,path,.5), "audio analysis cannot be overwritten by presets or OSC");
    }
    QString reason;
    check(engine.snapshotMatches(target, &reason), qPrintable("fresh capture matches: " + reason));
    editor.selectSnapshot(target);
    engine.setParameter(layer, "opacity", .37);
    QMenu menu;
    editor.fillMenu(&menu, layer, {"opacity"});
    QAction *selected = nullptr;
    QMenu *choices = nullptr;
    for (auto *action : menu.findChildren<QAction *>()) {
        if (action->text() == "Update in Selected Snapshot (Cue A)") selected = action;
        if (action->text() == "Update in Snapshot") choices = action->menu();
    }
    check(selected && selected->isEnabled() && choices && choices->actions().size() == 1, "named selected target and snapshot list");
    const auto before = engine.snapshot(index);
    if (selected) selected->trigger();
    auto expected = before.layers[0].toObject();
    auto params = expected.value("params").toObject(); params["opacity"] = engine.parameterValue(layer, "opacity"); expected["params"] = params;
    check(engine.snapshot(index).layers[0].toObject() == expected, "only requested value updated");
    check(engine.snapshot(index).composition == before.composition, "composition untouched");
    check(engine.snapshotMatches(target), "updated snapshot matches scene despite UI changes");
    undo.undo(); check(engine.snapshot(index).layers == before.layers, "undo restores stored values");
    undo.redo(); check(engine.snapshot(index).layers[0].toObject() == expected, "redo reapplies partial update");
    Mask mask = Mask::ellipse(QRectF(.2,.2,.5,.5));
    engine.setLayerMasks(layer, {mask});
    QMenu category;
    editor.fillCategoryMenu(&category, layer, "Masks");
    QAction *categoryUpdate = nullptr;
    for (auto *action : category.findChildren<QAction *>()) if (action->text() == "Update in Selected Snapshot (Cue A)") categoryUpdate = action;
    check(categoryUpdate, "category menu has snapshot update");
    if (categoryUpdate) categoryUpdate->trigger();
    check(engine.snapshot(index).layers[0].toObject().value("masks") == masks::toJson({mask}), "Masks tab captures geometry");
    undo.undo(); check(!engine.snapshot(index).layers[0].toObject().contains("masks"), "category update undo");
    engine.setParameter(0, "opacity", .63);
    editor.updateSnapshot(target, 0, {"opacity"});
    check(std::abs(engine.snapshot(index).composition.value("opacity").toDouble()-.63)<1e-6, "composition value update");
    const int matteRow = engine.addLayer("Matte");
    engine.layer(engine.indexOfId(layer))->matte.layer = engine.layerId(matteRow);
    editor.addToNew(layer, {QStringLiteral("category/%1").arg(Engine::PartMasks)});
    const auto preset = engine.preset(engine.indexOfPreset(editor.selected()));
    const auto masksCategory = preset.categories.value(QStringLiteral("%1:%2").arg(layer).arg(Engine::PartMasks)).toObject();
    check(masksCategory.value("matte") == engine.layer(engine.indexOfId(layer))->matte.toJson(), "mask preset includes its layer matte");
    editor.updateSnapshot(target, layer, {QStringLiteral("category/%1").arg(Engine::PartMasks)});
    check(engine.snapshot(index).layers[0].toObject().value("matte") == engine.layer(engine.indexOfId(layer))->matte.toJson(),
          "partial Masks snapshot update includes the matte");
    undo.undo();
    check(!engine.snapshot(index).layers[0].toObject().contains("matte"), "Undo removes a newly stored matte");
    Engine::Snapshot matteSnapshot; matteSnapshot.name="Matte"; matteSnapshot.fade=0;
    matteSnapshot.layers=engine.captureLayers(true);
    const int mi=engine.addSnapshot(matteSnapshot);
    engine.layer(engine.indexOfId(layer))->matte = {};
    check(!engine.snapshotMatches(engine.snapshot(mi).id), "snapshot concordance detects a changed matte");
    engine.recallSnapshot(mi);
    check(engine.layer(engine.indexOfId(layer))->matte.layer == engine.layerId(matteRow), "snapshot recall restores its matte");
    Engine::Preset firstPreset; firstPreset.name = "First preset";
    auto opacityValue = engine.presetValue(layer, "opacity"); opacityValue.time = .8; firstPreset.values = {opacityValue};
    const int firstIndex = engine.addPreset(firstPreset);
    const auto firstId = engine.preset(firstIndex).id;
    Engine::Preset otherPreset; otherPreset.name = "Other preset";
    otherPreset.values = {engine.presetValue(layer, "spatial/rotation")};
    const int otherIndex = engine.addPreset(otherPreset);
    const auto otherId = engine.preset(otherIndex).id;
    editor.select(firstId); engine.setParameter(layer, "opacity", .42);
    QMenu presetMenu; editor.fillMenu(&presetMenu, layer, {"opacity", "spatial/rotation"});
    QAction *updatePreset = nullptr;
    QMenu *presetChoices = nullptr;
    for (auto *action : presetMenu.findChildren<QAction *>()) {
        if (action->text() == "Update in Selected Preset (First preset)") updatePreset = action;
        if (action->text() == "Add / Update in Preset") presetChoices = action->menu();
    }
    check(updatePreset && updatePreset->isEnabled() && presetChoices, "preset menu offers named selected update and target list");
    editor.select(otherId); // actions must retain the target whose name was shown when opened
    if (updatePreset) updatePreset->trigger();
    const auto updatedPreset = engine.preset(firstIndex);
    check(updatedPreset.values.size() == 1 && std::abs(updatedPreset.values[0].value.toDouble() - .42) < 1e-6 &&
          updatedPreset.values[0].time == .8, "Update changes only existing entries of the originally selected preset and keeps their timing");
    undo.undo();
    check(engine.preset(firstIndex).values[0].value == opacityValue.value, "preset update is undoable");
    if (presetChoices) for (auto *action : presetChoices->actions()) if (action->text() == otherPreset.name) action->trigger();
    check(engine.preset(otherIndex).values.size() == 2 && engine.preset(otherIndex).indexOf(layer, "opacity") >= 0,
          "preset list adds the clicked values to any chosen preset without losing existing entries");
    undo.undo(); check(engine.preset(otherIndex).values.size() == 1, "adding to an arbitrary preset is undoable");
    std::printf("snapshot_update_ui: %d failures\n", failures);
    return failures ? 1 : 0;
}
