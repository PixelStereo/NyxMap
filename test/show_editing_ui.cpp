#include "Commands.h"
#include "Engine.h"
#include "PresetEditor.h"
#include "SequencePanel.h"
#include "SnapshotPanel.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QMouseEvent>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLineEdit>
#include <QShortcut>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUndoStack>
#include <iostream>
#include <cmath>
#include <stdexcept>
static void check(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
// A key on a list with the focus: first as the application asks (ahead of the menus' shortcuts), then pressed
static bool pressKey(QWidget *widget, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent ask(QEvent::ShortcutOverride, key, mods);
    ask.ignore();
    QApplication::sendEvent(widget, &ask);
    QKeyEvent down(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(widget, &down);
    return ask.isAccepted();
}
static void shortcut(QWidget *widget, const QKeySequence &key)
{
    for (auto *shortcut : widget->findChildren<QShortcut *>(QString(), Qt::FindDirectChildrenOnly))
        if (shortcut->key() == key) {
            emit shortcut->activated();
            return;
        }
    throw std::runtime_error("shortcut missing");
}
static QTreeWidgetItem *find(QTreeWidget *tree, const QString &tooltip)
{
    QTreeWidgetItemIterator it(tree);
    for (; *it; ++it)
        if ((*it)->toolTip(0) == tooltip)
            return *it;
    return nullptr;
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Engine engine;
    QUndoStack undo;
    try {
        const int layer = engine.addLayer("Layer");
        engine.layer(layer)->type = SourceType::Text;
        const auto id = engine.layerId(layer);
        Engine::Snapshot first, second;
        first.name = "First";
        first.layers = engine.captureLayers();
        second.name = "Second";
        engine.addSnapshot(first);
        engine.addSnapshot(second);
        Engine::Preset preset;
        preset.name = "Preset";
        const auto pi = engine.addPreset(preset);
        PresetEditor editor(&engine, &undo);
        SnapshotPanel panel(&engine, &undo);
        panel.resize(1000, 650);
        panel.show();
        app.processEvents();
        auto *library = panel.listPanel()->findChild<QTreeWidget *>("snapshotList"),
             *values = panel.findChild<QTreeWidget *>("snapshotValues"),
             *presets = panel.listPanel()->findChild<QTreeWidget *>("presetList"),
             *presetValues = panel.findChild<QTreeWidget *>("presetValues");
        check(library && values && presets && presetValues, "stored-content trees present");
        library->setCurrentItem(library->topLevelItem(0));
        auto *opacity = find(values, "opacity");
        check(opacity, "stored opacity row");
        values->setCurrentItem(opacity);
        shortcut(values, QKeySequence(QKeySequence::Copy));
        library->setCurrentItem(library->topLevelItem(1));
        shortcut(values, QKeySequence(QKeySequence::Paste));
        check(engine.snapshot(1).partial && engine.snapshot(1).layers.size() == 1,
              "paste into empty snapshot is partial");
        check(engine.snapshot(1).layers[0].toObject().value("params").toObject().size() == 1,
              "only copied value pasted");
        undo.undo();
        check(engine.snapshot(1).layers.isEmpty(), "snapshot paste undoes");
        undo.redo();
        check(engine.snapshot(1).layers.size() == 1, "snapshot paste redoes");
        engine.setParameter(id, "opacity", .33);
        panel.findChild<QPushButton *>("snapshotUpdate")->click();
        check(engine.snapshot(1).layers[0].toObject().value("params").toObject().size() == 1 &&
                  std::abs(engine.snapshot(1).layers[0].toObject().value("params").toObject().value("opacity").toDouble() - .33) < 1e-5,
              "partial snapshot update keeps deleted contents out");
        undo.undo();
        panel.findChild<QPushButton *>("snapshotDuplicate")->click();
        check(engine.snapshotCount() == 3 && engine.snapshot(1).id != engine.snapshot(2).id,
              "snapshot duplicate has new identity");
        const auto duplicateId = engine.snapshot(2).id;
        undo.undo();
        check(engine.snapshotCount() == 2, "duplicate undo");
        undo.redo();
        check(engine.snapshot(2).id == duplicateId, "duplicate redo retains identity");
        presets->setCurrentItem(presets->topLevelItem(pi));
        panel.setMode(1);
        shortcut(presetValues, QKeySequence(QKeySequence::Paste));
        check(engine.preset(pi).indexOf(id, "opacity") >= 0, "copy snapshot value into preset");
        presetValues->setCurrentItem(presetValues->topLevelItem(0));
        shortcut(presetValues, QKeySequence(QKeySequence::Delete));
        check(engine.preset(pi).values.empty(), "delete preset owner removes stored contents");
        undo.undo();
        check(!engine.preset(pi).values.empty(), "preset removal undoes");
        panel.findChild<QPushButton *>("presetDuplicate")->click();
        check(engine.presetCount() == 2 && engine.preset(0).id != engine.preset(1).id,
              "preset duplicate has fresh identity");
        // The same bar as the snapshots': the list's duplicate, not the inspector's; groups, moves, undo
        auto *duplicate = panel.findChild<QPushButton *>("presetDuplicate");
        check(duplicate->text() == QStringLiteral("⧉") && duplicate->parentWidget()->isAncestorOf(presets),
              "preset duplicate sits above the list");
        // The keys of a list: Return never plays, the modifier does; the list takes them ahead of the menus' shortcuts
        {
            int played = 0;
            auto counting = QObject::connect(&panel, &SnapshotPanel::recalled, &panel, [&] { ++played; });
            presets->setCurrentItem(presets->topLevelItem(0));
            check(pressKey(presets, Qt::Key_Return) && played == 0, "Return does not play a preset");
            check(pressKey(presets, Qt::Key_Return, Qt::ControlModifier) && played == 1, "⌘Return does");
            const int count = engine.presetCount();
            QKeyEvent held(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier, QString(), true);
            QApplication::sendEvent(presets, &held);
            check(played == 1, "a held key plays once");
            check(pressKey(presets, Qt::Key_D, Qt::ControlModifier) && engine.presetCount() == count + 1, "⌘D duplicates");
            check(pressKey(presets, Qt::Key_Delete) && engine.presetCount() == count, "Delete deletes");
            app.processEvents();
            library->setCurrentItem(library->topLevelItem(0));
            check(pressKey(library, Qt::Key_Return) && played == 1, "Return does not play a snapshot");
            check(pressKey(library, Qt::Key_Return, Qt::ControlModifier) && played == 2, "⌘Return does");
            // A double-click renames, in the row, and never plays; the preset's inspector has no name of its own
            for (auto *list : {presets, library}) {
                list->setCurrentItem(list->topLevelItem(0));
                emit list->itemDoubleClicked(list->topLevelItem(0), 1);
                check(played == 2, "a double-click does not play");
                list->topLevelItem(0)->setText(1, "Renamed here");
                app.processEvents();
                app.processEvents();
            }
            check(engine.preset(0).name == "Renamed here" && engine.snapshot(0).name == "Renamed here", "the name typed in a row is taken");
            check(presets->topLevelItem(0)->flags() & Qt::ItemIsEditable, "a preset's row can be renamed");
            QObject::disconnect(counting);
            // The filter: whatever the case; the selected row stays; emptied, all is back
            auto *filter = panel.listPanel()->findChild<QLineEdit *>("presetFilter");
            check(filter != nullptr, "the presets' filter");
            if (filter) {
                presets->setCurrentItem(nullptr);
                filter->setText(engine.preset(0).name.toUpper());
                check(!presets->topLevelItem(0)->isHidden(), "the filter finds a name, whatever its case");
                filter->setText("no such name");
                check(presets->topLevelItem(0)->isHidden(), "and hides the others");
                filter->clear();
                check(!presets->topLevelItem(0)->isHidden(), "emptied, all is back");
            }
        }
        app.processEvents();
        const auto barOf = [&](std::initializer_list<const char *> names) {
            int last = -1;
            bool ordered = true;
            for (const char *name : names) {
                auto *button = panel.findChild<QPushButton *>(name);
                const int at = button ? button->x() : -1;
                ordered = ordered && at > last;
                last = at;
            }
            return ordered;
        };
        check(barOf({"presetGroup", "presetDelete", "presetUpdate", "presetDuplicate"}), "preset bar in the snapshots' order");
        const int presetsBefore = engine.presetCount();
        const quint64 grouped = engine.preset(1).id;
        panel.findChild<QPushButton *>("presetGroup")->click();
        app.processEvents();
        auto tree = engine.presetTree();
        check(int(tree.size()) == presetsBefore + 1 && !tree.back().snapshot, "a new group of presets");
        const quint64 group = tree.back().group;
        check(group != 0, "the group has an id");
        {
            auto after = tree;
            Engine::SnapshotNode member;
            for (auto it = after.begin(); it != after.end(); ++it)
                if (it->snapshot == grouped) {
                    member = *it;
                    after.erase(it);
                    break;
                }
            after.back().children.push_back(member);
            undo.push(new cmd::SetLibraryTree([&engine](const auto &t) { engine.setPresetTree(t); }, tree, after, "Move"));
        }
        app.processEvents();
        check(engine.presetTree().back().children.size() == 1 && engine.presetTree().back().children[0].snapshot == grouped, "a preset in its group");
        QTreeWidgetItem *groupRow = nullptr;
        for (int k = 0; k < presets->topLevelItemCount(); ++k)
            if (presets->topLevelItem(k)->childCount()) groupRow = presets->topLevelItem(k);
        check(groupRow && groupRow->child(0), "the list shows it inside");
        QTemporaryDir treeDir;
        QString treeError;
        const QString treeProject = treeDir.filePath("tree.nyxmap");
        check(engine.saveProject(treeProject, {}, &treeError), "saved with its groups");
        {
            Engine reopened;
            check(reopened.loadProject(treeProject, nullptr, &treeError), "opened again");
            const auto kept = reopened.presetTree();
            check(!kept.empty() && !kept.back().snapshot && kept.back().children.size() == 1 && kept.back().children[0].snapshot == grouped,
                  "its groups come back with the project");
        }
        undo.undo();
        check(engine.presetTree().back().children.empty(), "the move undoes");
        undo.undo();
        check(int(engine.presetTree().size()) == presetsBefore, "the new group undoes");
        // Add elements: any value, those the inspector shows or not, Enable and Solo too
        editor.select(engine.preset(pi).id);
        QMenu elements;
        editor.fillAddElements(&elements);
        QMenu *layerMenu = nullptr;
        for (QAction *a : elements.actions())
            if (a->menu() && a->text() == engine.layer(engine.indexOfId(id))->name) layerMenu = a->menu();
        check(layerMenu != nullptr, "Add elements lists the layer");
        QAction *enableAction = nullptr;
        if (layerMenu)
            for (QAction *a : layerMenu->actions())
                if (a->menu())
                    for (QAction *value : a->menu()->actions())
                        if (value->toolTip() == "enable") enableAction = value;
        check(enableAction != nullptr, "Enable can be chosen");
        if (enableAction) enableAction->trigger();
        check(engine.preset(pi).indexOf(id, "enable") >= 0, "Enable goes in the preset");
        QCheckBox enableBox;
        editor.track(&enableBox, id, {"enable"});
        editor.setEditing(true);
        check(editor.inSelected(id, {"enable"}), "the switch in the inspector knows it is in");
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(2, 2), QPointF(2, 2), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&enableBox, &press);
        check(engine.preset(pi).indexOf(id, "enable") < 0, "a click on it in Edit mode takes it out");
        editor.setEditing(false);
        undo.undo();
        undo.undo();
        Engine::Preset visibility;
        visibility.values.push_back({id, "enable", true, 0, QString()});
        const int vi = engine.addPreset(visibility);
        engine.setParameter(id, "enable", false);
        undo.push(new cmd::RecallPreset(&engine, engine.preset(vi).id));
        check(engine.layer(engine.indexOfId(id))->enabled, "copied visibility can be recalled");
        undo.undo();
        check(!engine.layer(engine.indexOfId(id))->enabled, "visibility recall undo restores transient state");
        // Sequence multi-selection, groups, clipboard, and editing mode all operate through the actual UI.
        Sequencer::Step a, b, c;
        a.snapshot = engine.snapshot(0).id;
        a.at = .2;
        b.preset = engine.preset(0).id;
        c.description = "Third";
        Sequencer::Sequence sequence;
        sequence.steps = {a, b, c};
        const int qi = engine.sequencer().add(sequence);
        engine.sequencer().setCurrent(qi);
        SequenceWindow window(&engine, nullptr, &undo);
        window.show();
        auto *steps = window.findChild<QTableWidget *>("sequenceSteps");
        check(steps, "sequence table");
        steps->setCurrentCell(0, 1);
        window.findChild<QPushButton *>("sequenceDuplicateSteps")->click();
        auto edited = engine.sequencer().sequence(qi);
        check(edited.steps.size() == 4 && edited.steps[0].runtimeId != edited.steps[1].runtimeId &&
                  edited.steps[1].at < 0,
              "duplicate cue keeps fresh identity and clears duplicate timecode");
        undo.undo();
        check(engine.sequencer().sequence(qi).steps.size() == 3, "cue duplication undo");
        steps->clearSelection();
        steps->setRangeSelected(QTableWidgetSelectionRange(0, 0, 1, steps->columnCount() - 1), true);
        pressKey(steps, Qt::Key_G, Qt::ControlModifier);
        edited = engine.sequencer().sequence(qi);
        check(edited.steps.size() == 2 && edited.steps[0].actions.size() == 2, "selected cues become one action group");
        QMenu groupMenu;
        window.fillStepMenu(&groupMenu, 0);
        bool canCreate = false, oldEditor = false;
        for (auto *action : groupMenu.actions()) {
            canCreate = canCreate || action->text() == "Create Group";
            oldEditor = oldEditor || action->text().contains("action group");
        }
        check(canCreate && !oldEditor, "groups are made by the menu; no window of their actions any more");
        // In the list, as the layers: the group, its steps under it, numbered; folded, only the group
        check(steps->rowCount() == 4 && window.actionOfRow(1) == 0 && window.stepOfRow(2) == 0 && window.stepOfRow(3) == 1,
              "a group's steps are rows under it");
        check(steps->item(1, 0)->text() == "1.1" && steps->item(3, 0)->text() == "2", "numbered inside it");
        check(!(steps->item(1, 5)->flags() & Qt::ItemIsEditable), "the group says when its steps start");
        window.toggleFold(0);
        check(steps->rowCount() == 2 && steps->item(1, 0)->text() == "2", "folded: only the group");
        const int undoAtFold = undo.index();
        check(Sequencer::stepToJson(engine.sequencer().sequence(qi).steps[0]).value("closed").toBool(), "folded is saved");
        emit steps->cellDoubleClicked(0, 1);
        check(steps->rowCount() == 4 && undo.index() == undoAtFold, "a double-click opens it, not an undo step");
        // A step of the group edited in its row
        steps->item(2, 7)->setText("Second inside");
        check(engine.sequencer().sequence(qi).steps[0].actions[1].description == "Second inside", "a step of a group has its description");
        undo.undo();
        // Out of the group and in again, as a drop does; ▲ too
        int top = -1, inside = -1;
        window.dropPlace(steps->rowViewportPosition(2) + 2, &top, &inside);
        check(top == 0 && inside == 1, "dropped on the upper half of a step of a group: before it, in the group");
        window.dropPlace(steps->rowViewportPosition(0) + steps->rowHeight(0) - 2, &top, &inside);
        check(top == 0 && inside == 0, "on the lower half of the group's row: into it");
        window.dropPlace(steps->rowViewportPosition(3) + 2, &top, &inside);
        check(top == 1 && inside == -1, "before the step after the group: out of it");
        window.moveRowsTo({2}, 1, -1);
        edited = engine.sequencer().sequence(qi);
        check(edited.steps.size() == 3 && edited.steps[0].actions.size() == 1 && edited.steps[1].preset == engine.preset(0).id,
              "a step dragged out of a group");
        undo.undo();
        window.moveRowsTo({3}, 0, 1);
        edited = engine.sequencer().sequence(qi);
        check(edited.steps.size() == 1 && edited.steps[0].actions.size() == 3 && edited.steps[0].actions[1].description == "Third" &&
                  edited.steps[0].actions[1].next == Sequencer::StepContinue::Wait, "a step dragged into a group");
        undo.undo();
        window.moveRowsTo({1, 2}, 2, -1);
        check(engine.sequencer().sequence(qi).steps.size() == 3 && engine.sequencer().sequence(qi).steps[0].actions.empty(),
              "a group whose last step left goes");
        undo.undo();
        steps->setCurrentCell(3, 1);
        QPushButton *up = nullptr;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("▲")) up = button;
        check(up != nullptr, "the move button");
        if (up) up->click();
        check(engine.sequencer().sequence(qi).steps.size() == 1 && engine.sequencer().sequence(qi).steps[0].actions.size() == 3,
              "▲ takes a step into the open group above it");
        undo.undo();
        // A group played one GO per step (Start First And Enter): its steps stay steps of the sequence
        {
            QMenu modeMenu;
            window.fillStepMenu(&modeMenu, 0);
            QAction *oneGo = nullptr;
            for (auto *action : modeMenu.actions())
                if (action->menu() && action->text() == "Group mode")
                    for (auto *mode : action->menu()->actions())
                        if (mode->text() == "One GO per step") oneGo = mode;
            check(oneGo != nullptr, "the group's mode in its menu");
            if (oneGo) oneGo->trigger();
            edited = engine.sequencer().sequence(qi);
            check(edited.steps.size() == 3 && edited.steps[0].actions.empty() && edited.steps[0].folder &&
                      edited.steps[1].folder == edited.steps[0].folder && !edited.steps[2].folder, "its steps are steps again, gathered");
            check(steps->rowCount() == 4 && window.actionOfRow(0) == SequenceWindow::kHeaderRow && steps->item(1, 0)->text() == "1" && steps->item(2, 0)->text() == "2",
                  "the group's row above them, their own numbers");
            check(steps->item(0, 1)->text().contains("2 GOs"), "the row says how it plays");
            check(steps->item(1, 5)->flags() & Qt::ItemIsEditable, "each has its quantize");
            // Each GO plays one
            int goes = 0;
            auto counting = QObject::connect(&engine.sequencer(), &Sequencer::positionChanged, &window, [&] { ++goes; });
            engine.sequencer().setPosition(-1);
            engine.sequencer().go();
            engine.sequencer().advance(.01);
            check(engine.sequencer().position() == 0, "GO plays its first step");
            engine.sequencer().go();
            engine.sequencer().advance(.01);
            check(engine.sequencer().position() == 1, "the next GO its second");
            engine.sequencer().go();
            engine.sequencer().advance(.01);
            check(engine.sequencer().position() == 2, "then the step after the group");
            engine.sequencer().stopAll();
            engine.sequencer().setPosition(-1);
            QObject::disconnect(counting);
            // Folded, saved, moved in and out
            window.toggleFolder(0);
            check(steps->rowCount() == 2 && window.rowOf(1) == 0, "folded: its row stands for its steps");
            const auto savedSequences = engine.sequencer().toJson();
            check(savedSequences[qi].toObject().value("closed_folders").toArray().size() == 1 &&
                      savedSequences[qi].toObject().value("steps").toArray()[0].toObject().contains("folder"), "saved");
            window.toggleFolder(0);
            quint64 folder = 0;
            int top = -1, inside = -1;
            window.dropPlace(steps->rowViewportPosition(3) + 2, &top, &inside, &folder);
            check(top == 2 && inside == -1 && folder == 0, "above the step after the group: out");
            window.dropPlace(steps->rowViewportPosition(2) + 2, &top, &inside, &folder);
            check(top == 1 && folder == edited.steps[0].folder, "between its steps: in it");
            window.dropPlace(steps->rowViewportPosition(0) + 2, &top, &inside, &folder);
            check(top == 0 && folder == 0, "above its row: before it");
            window.moveRowsTo({3}, 1, -1, edited.steps[0].folder);
            auto moved = engine.sequencer().sequence(qi);
            check(moved.steps.size() == 3 && moved.steps[1].folder == edited.steps[0].folder && moved.steps[1].description == "Third", "a step dragged in");
            undo.undo();
            window.moveRowsTo({1}, 3, -1);
            moved = engine.sequencer().sequence(qi);
            check(!moved.steps[2].folder && moved.steps[0].folder, "a step dragged out");
            undo.undo();
            // Back to a group of steps together, as it was
            window.setGroupTogether(0);
            edited = engine.sequencer().sequence(qi);
            check(edited.steps.size() == 2 && edited.steps[0].actions.size() == 2 && !edited.steps[0].folder, "and back to all together");
            undo.undo();
            undo.undo();
            check(engine.sequencer().sequence(qi).steps[0].actions.size() == 2, "the mode's change undone");
        }
        // The Group button: without a selection, a new group with a first step
        steps->clearSelection();
        steps->setCurrentItem(nullptr);
        window.findChild<QPushButton *>("sequenceGroupSteps")->click();
        edited = engine.sequencer().sequence(qi);
        check(edited.steps.size() == 3 && edited.steps[2].actions.size() == 1, "the Group button with nothing selected");
        undo.undo();
        steps->setCurrentCell(2, 1);
        pressKey(steps, Qt::Key_Delete);
        check(engine.sequencer().sequence(qi).steps[0].actions.size() == 1, "a step of a group deleted");
        undo.undo();
        undo.undo();
        check(engine.sequencer().sequence(qi).steps.size() == 3, "grouping undo");
        steps->setCurrentCell(0, 1);
        shortcut(steps, QKeySequence(QKeySequence::Copy));
        steps->setCurrentCell(2, 1);
        shortcut(steps, QKeySequence(QKeySequence::Paste));
        check(engine.sequencer().sequence(qi).steps.size() == 4, "cue clipboard paste");
        pressKey(steps, Qt::Key_Delete);
        check(engine.sequencer().sequence(qi).steps.size() == 3, "cue keyboard delete");
        undo.undo();
        check(engine.sequencer().sequence(qi).steps.size() == 4, "cue delete undo");
        auto *editMode = window.findChild<QCheckBox *>("sequenceEditMode");
        check(editMode, "timecode edit control");
        editMode->setChecked(true);
        check(engine.sequencer().sequence(qi).editMode, "timecode edit control persists");
        undo.undo();
        check(!engine.sequencer().sequence(qi).editMode, "edit mode undo");
        std::cout << "Snapshot/preset transfer, removal, duplication, undo; sequence clipboard, duplicate, group and "
                     "edit controls passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
