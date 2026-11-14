// The panel of the steps: a cue edited while it runs, the continuations and the timecodes typed in their cells.
#include "SequencePanel.h"
#include <QApplication>
#include <QJsonArray>
#include <QThread>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QKeyEvent>
#include <QMenu>
#include <QUndoStack>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <cstdio>
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("NyxMapSequenceUITest");
    Engine engine;
    Sequencer::Sequence sequence;
    Sequencer::Step first, second, third;
    first.preWait = 2;
    first.postWait = 4;
    first.next = Sequencer::StepContinue::AutoFollow;
    second.description = QString(2000, QChar('A'));
    sequence.steps = {first, second, third};
    engine.sequencer().add(sequence);
    engine.sequencer().setCurrent(0);
    int failures = 0;
    auto check = [&](bool ok, const char *reason) { if (!ok) { ++failures; std::printf("FAIL sequence_ui: %s\n", reason); } };
    check(engine.sequencer().toJson()[0].toObject()["steps"].toArray()[1].toObject()["description"].toString() == second.description, "description serialized");
    engine.sequencer().go();
    const double elapsedBeforeEdit = engine.sequencer().stepElapsed(0);
    auto editedSequence = engine.sequencer().sequence(0);
    editedSequence.steps[0].description = "Edited while running";
    engine.sequencer().setSequence(0, editedSequence);
    check(engine.sequencer().stepElapsed(0) >= elapsedBeforeEdit, "editing a cue preserves its elapsed clock");
    for (double dt : {1.0, 1.0, 2.0, 2.0}) engine.sequencer().advance(dt);
    QThread::msleep(20); check(engine.sequencer().stepElapsed(0) > 0, "elapsed clock survives cue completion");
    engine.sequencer().stopAll();
    Sequencer::Sequence overview;
    overview.steps.resize(3);
    overview.steps[1].next = Sequencer::StepContinue::AutoFollow;
    overview.steps[2].next = Sequencer::StepContinue::AutoFollow;
    engine.sequencer().setCurrent(engine.sequencer().add(overview));
    QUndoStack undo;
    SequenceWindow editor(&engine, nullptr, &undo);
    auto *steps = editor.findChild<QTableWidget *>("sequenceSteps");
    if (const QByteArray shots = qgetenv("NYXMAP_TEST_SHOTS"); !shots.isEmpty()) { // a picture of the window, for a look
        editor.show();
        editor.resize(1100, 420);
        QCoreApplication::processEvents();
        editor.grab().save(QString::fromLocal8Bit(shots) + "/sequences.png");
    }
    check(steps && steps->horizontalHeader()->visualIndex(6) == 4 && steps->horizontalHeader()->visualIndex(1) == 3,
          "continuation sits beside the target before timing columns");
    if (steps) {
        check(steps->item(2, 6)->toolTip().contains("endless"), "AUTO explains a non-terminating action");
        steps->item(1, 6)->setData(Qt::EditRole, int(Sequencer::StepContinue::Wait));
        check(steps->item(1, 6)->data(Qt::AccessibleTextRole).toString() == "GO" &&
              steps->item(1, 6)->toolTip().contains("manual"), "edited mode updates its accessible label and help");
        // The waits on one Time cell, changed together in its small window: Return keeps them, Esc does not
        auto waits = [&] { const auto st = engine.sequencer().sequence(engine.sequencer().current()).steps[2]; return std::pair(st.preWait, st.postWait); };
        check(steps->isColumnHidden(2) && steps->isColumnHidden(3) && steps->isColumnHidden(4) && !steps->isColumnHidden(9), "one Time column");
        check(steps->item(2, 9)->toolTip().contains("Pre-wait") && steps->item(2, 9)->toolTip().contains("Post-wait"), "its tooltip tells the times");
        editor.show();
        auto timeBox = [&](const char *name) {
            auto *window = editor.findChild<QFrame *>("stepTimeEditor");
            return window ? window->findChild<QDoubleSpinBox *>(name) : nullptr;
        };
        auto key = [&](QWidget *to, int code) {
            QKeyEvent down(QEvent::KeyPress, code, Qt::NoModifier);
            QApplication::sendEvent(to, &down);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        };
        editor.openTimeEditor(2);
        check(timeBox("stepPre") && timeBox("stepPost") && timeBox("stepPost")->isEnabled(), "an AUTO step's post-wait can be changed");
        if (timeBox("stepPre")) {
            timeBox("stepPre")->setValue(1.5);
            timeBox("stepPost")->setValue(2.5);
            key(timeBox("stepPre"), Qt::Key_Return);
            check(waits() == std::pair(1.5, 2.5), "Return keeps the waits");
        }
        check(!editor.findChild<QFrame *>("stepTimeEditor"), "and the window goes");
        editor.openTimeEditor(2);
        if (timeBox("stepPre")) {
            timeBox("stepPre")->setValue(9);
            key(timeBox("stepPre"), Qt::Key_Escape);
            check(waits() == std::pair(1.5, 2.5), "Esc does not");
        }
        // The timecode, first: typed in seconds or minutes:seconds; an empty cell offers the time of the step before
        const int at = 8; // ColAt
        auto edited = [&] { return engine.sequencer().sequence(engine.sequencer().current()); };
        check(steps->horizontalHeader()->visualIndex(at) == 1, "the timecode comes first");
        check(editor.proposedTimecode(0) == "0:00.00", "an empty cell offers 0 with nothing before it");
        steps->item(0, at)->setText("12");
        check(edited().steps[0].at == 12 && steps->item(0, at)->text() == "0:12.00", "12 is 12 seconds");
        steps->item(1, at)->setText("1:02.5");
        check(edited().steps[1].at == 62.5, "minutes:seconds");
        check(editor.proposedTimecode(2) == "1:02.50", "an empty cell offers the time of the step before");
        steps->item(1, at)->setText("");
        check(edited().steps[1].at < 0 && steps->item(1, at)->text().isEmpty(), "emptied: no timecode");
    }
    if (steps) {
        Engine::Snapshot initial;
        initial.name = "Initial look";
        initial.composition = {{"opacity", 0.25}, {"volume", 0.75}};
        const auto snapshot = engine.snapshot(engine.addSnapshot(initial)).id;
        Engine::Preset next;
        next.name = "Lift the light";
        next.values = {{0, "opacity", 0.5}, {0, "volume", 0.75}};
        const auto preset = engine.preset(engine.addPreset(next)).id;
        Sequencer::Sequence comparison;
        comparison.steps.resize(3);
        comparison.steps[0].snapshot = snapshot;
        comparison.steps[1].preset = preset;
        comparison.steps[2].preset = preset;
        engine.sequencer().setCurrent(engine.sequencer().add(comparison));
        auto *changes = editor.findChild<QPlainTextEdit *>("sequenceStepChanges");
        steps->setCurrentCell(1, 1);
        const QString diff = changes ? changes->toPlainText() : QString();
        check(diff.contains("opacity") && diff.contains("0.25 → 0.5") && !diff.contains("volume"),
              "selected preset lists only changes from the preceding snapshot");
        steps->setCurrentCell(2, 1);
        check(changes && changes->toPlainText().contains("No stored changes."), "a repeated preset has no differences");
        check(engine.sequencer().position() == -1, "inspecting differences never triggers GO");
        auto simplify = [&](int row) {
            QMenu menu;
            editor.fillStepMenu(&menu, row);
            for (auto *action : menu.actions())
                if (action->text() == "Simplify Against Previous Cue" && action->isEnabled()) {
                    action->trigger();
                    return true;
                }
            return false;
        };
        auto current = [&] { return engine.sequencer().sequence(engine.sequencer().current()); };
        check(simplify(1), "the preset's context menu offers simplification");
        const quint64 delta = current().steps[1].preset;
        const auto simplified = engine.preset(engine.indexOfPreset(delta));
        check(delta != preset && current().steps[2].preset == preset && simplified.values.size() == 1 &&
                  simplified.indexOf(0, "opacity") >= 0 && engine.preset(engine.indexOfPreset(preset)).values.size() == 2,
              "a shared preset is copied and only the changed value remains");
        undo.undo();
        check(current().steps[1].preset == preset && engine.indexOfPreset(delta) < 0, "undo restores cue and preset library");
        undo.redo();
        check(current().steps[1].preset == delta && engine.indexOfPreset(delta) >= 0, "redo restores the delta");
        check(simplify(1) && current().steps[1].preset == delta, "simplifying an existing delta keeps its reference");

        initial.composition["opacity"] = 0.6;
        const auto secondSnapshot = engine.snapshot(engine.addSnapshot(initial)).id;
        comparison.steps[1].preset = 0;
        comparison.steps[1].snapshot = secondSnapshot;
        const int sequenceIndex = engine.sequencer().add(comparison);
        engine.sequencer().setCurrent(sequenceIndex);
        check(simplify(1), "the snapshot's context menu offers simplification");
        const auto converted = current().steps[1];
        const auto convertedPreset = engine.preset(engine.indexOfPreset(converted.preset));
        check(!converted.snapshot && convertedPreset.values.size() == 1 &&
                  convertedPreset.indexOf(0, "opacity") >= 0 && engine.indexOfSnapshot(secondSnapshot) >= 0,
              "the snapshot cue becomes a delta preset and the library snapshot is preserved");

    }
    return failures ? 1 : 0;
}
