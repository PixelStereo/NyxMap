// The Show Control tab: the desk follows the sequence, GO, GO BACK and the stops play it, the cue stack and the libraries launch,
// and it follows what something else plays.
#include "Engine.h"
#include "MainWindow.h"
#include "ShowControlPanel.h"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <cstdio>

int runShowControlUiTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *reason) {
        if (!ok) { ++failures; std::printf("FAIL show_control_ui: %s\n", reason); }
    };
    for (const char *name : {"Opening", "Finale"}) {
        Engine::Snapshot snapshot;
        snapshot.name = name;
        engine.addSnapshot(snapshot);
    }
    Engine::Preset preset;
    preset.name = "Warm";
    engine.addPreset(preset);
    Animation timeline;
    timeline.name = "Sweep";
    timeline.duration = 8;
    engine.addAnimation(timeline);
    Sequencer::Sequence sequence;
    sequence.name = "Main";
    for (int k = 0; k < 2; ++k) {
        Sequencer::Step step;
        step.snapshot = engine.snapshot(k).id;
        step.text = k ? "Bow" : "Lights up";
        sequence.steps.push_back(step);
    }
    auto &sq = engine.sequencer();
    sq.setCurrent(sq.add(sequence));

    ShowControlPanel panel(&engine);
    panel.resize(420, 900);
    panel.show();
    qApp->processEvents();
    panel.refresh();

    auto *tabs = panel.findChild<QTabWidget *>("showControlTabs");
    check(tabs && tabs->count() == 4 && tabs->tabText(0) == "SHOW" && tabs->tabText(3) == "TIMELINES", "its four pages");
    auto *combo = panel.findChild<QComboBox *>("showSequence");
    check(combo && combo->count() == 1 && combo->currentText().contains("Main"), "the selected sequence");
    auto *go = panel.findChild<QPushButton *>("showGo");
    auto *back = panel.findChild<QPushButton *>("showBack");
    auto *current = panel.findChild<QLabel *>("cueLabel_current");
    auto *next = panel.findChild<QLabel *>("cueLabel_next");
    check(go && back && current && next, "GO, GO BACK and the cards");
    if (!go || !back || !current || !next) return failures;
    check(go->isEnabled() && !back->isEnabled(), "before the first GO, GO only");
    check(current->text() == QStringLiteral("—") && next->text().startsWith("1"), "nothing played, the first cue next");

    go->click();
    panel.refresh();
    check(sq.position() == 0, "GO plays the first cue");
    check(current->text().startsWith("1") && next->text().startsWith("2"), "the cards follow it");

    auto *cue2 = panel.findChild<QPushButton *>("cue_2");
    check(cue2 && cue2->text().contains("Bow"), "the cue stack, by its text");
    if (cue2) cue2->click();
    panel.refresh();
    check(sq.position() == 1 && current->text().startsWith("2"), "a cue of the stack plays it");

    back->click();
    panel.refresh();
    check(sq.position() == 0, "GO BACK plays the one before");

    sq.go(); // something else plays the show
    panel.refresh();
    check(current->text().startsWith("2"), "and it follows what something else plays");

    auto *snapshot = panel.findChild<QPushButton *>("snapshot_2");
    auto *warm = panel.findChild<QPushButton *>("preset_1");
    check(snapshot && snapshot->text().contains("Finale") && warm && warm->text().contains("Warm"), "a button per snapshot and preset");
    if (snapshot) snapshot->click();

    const quint64 timelineId = engine.animation(0).id;
    QPushButton *play = nullptr;
    for (auto *b : panel.findChildren<QPushButton *>())
        if (b->text() == "PLAY") play = b;
    check(play != nullptr, "a timeline's transport");
    if (play) play->click();
    std::vector<Engine::AnimationPlayback> playback;
    engine.sampleAnimations(&playback);
    bool playing = false;
    for (const auto &a : playback) playing = playing || (a.id == timelineId && a.state == AnimState::Playing);
    check(playing, "PLAY plays the timeline");
    engine.controlAnimation(timelineId, AnimAction::Stop);

    // The list follows the project
    Engine::Snapshot later;
    later.name = "Encore";
    engine.addSnapshot(later);
    for (int k = 0; k < 6; ++k) panel.refresh();
    check(panel.findChild<QPushButton *>("snapshot_3") != nullptr, "a new snapshot appears");

    int layers = 0;
    QObject::connect(&panel, &ShowControlPanel::layersRequested, &panel, [&] { ++layers; });
    for (auto *b : panel.findChildren<QPushButton *>())
        if (b->text().startsWith("LAYER CONTROL")) b->click();
    check(layers == 1, "LAYER CONTROL leads to the layers");

    // In the window: a tab of the left pane, after the others
    {
        MainWindow window(&engine);
        window.setQuiet(true);
        window.setAutosaveEnabled(false);
        window.show();
        qApp->processEvents();
        QTabWidget *left = nullptr;
        int index = -1;
        for (auto *candidate : window.findChildren<QTabWidget *>())
            for (int k = 0; k < candidate->count(); ++k)
                if (candidate->tabText(k) == QLatin1String("Show Control")) { left = candidate; index = k; }
        check(left && index == left->count() - 1, "the window has a Show Control tab, the last on the left");
        if (left) {
            left->setCurrentIndex(index);
            qApp->processEvents();
            check(left->currentWidget()->findChild<QPushButton *>("showGo") != nullptr, "and it shows the desk");
            for (auto *b : left->currentWidget()->findChildren<QPushButton *>())
                if (b->text().startsWith("LAYER CONTROL")) b->click();
            check(left->currentIndex() == 1, "LAYER CONTROL opens the Layers tab");
        }
    }

    qApp->processEvents();
    if (qEnvironmentVariableIsSet("NYXMAP_SHOW_CONTROL_SHOT")) panel.grab().save(qEnvironmentVariable("NYXMAP_SHOW_CONTROL_SHOT"));
    std::printf("show_control_ui: %d failures\n", failures);
    return failures;
}
