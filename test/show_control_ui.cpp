#include <QAction>
// The Show Control tab: the desk follows the sequence, GO, GO BACK and the stops play it, the cue stack and the libraries launch,
// and it follows what something else plays.
#include "Engine.h"
#include "MainWindow.h"
#include "SequencePanel.h"
#include "ShowControlPanel.h"
#include "TimelineActivity.h"
#include "TimelinePanel.h"
#include <QHeaderView>
#include <QElapsedTimer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTabBar>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
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
        if (k) step.description = "Bow"; // the second by its description, the first by its target
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
    check(cue2 && cue2->text().contains("Bow"), "the cue stack, by its description");
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

    // The desktop keeps sequence editing on the left and opens the show desk in a browser.
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
                if (candidate->tabText(k) == QLatin1String("Sequences")) { left = candidate; index = k; }
        check(left && index > 0 && index + 1 < left->count() &&
                  left->tabText(index - 1) == "Controls" && left->tabText(index + 1) == "Logs",
              "Sequences sits between Controls and Logs");
        check(!window.findChild<ShowControlPanel *>(), "the desktop window leaves Show Control to the browser");
        check(window.findChild<QAction *>("openShowControlWeb"), "the Sequence menu can open the web desk");
        if (left) {
            left->setCurrentIndex(index);
            qApp->processEvents();
            check(left->currentWidget()->findChild<SequenceWindow *>(), "the sequence editor is in the left tab");
        }
        auto *columns = window.findChild<QSplitter *>("mainColumns");
        check(columns && columns->orientation() == Qt::Horizontal &&
                  columns->widget(0)->height() == columns->widget(1)->height(),
              "the left column spans the full height of the workspace");
        if (auto *interactions = window.findChild<QSplitter *>("interactionSplit")) {
            if (left && index >= 2) left->setCurrentIndex(index - 2);
            qApp->processEvents();
            interactions->setSizes({0, 1000});
            check(interactions->sizes().value(0) >= 100, "the interaction list cannot collapse");
            interactions->setSizes({180, 540});
            if (left) left->setCurrentIndex(index);
        } else {
            check(false, "the interaction splitter is present");
        }
        // The steps with a menu of the sequences on top and their properties under them: no tabs, no Timelines button
        auto *sequences = window.findChild<SequenceWindow *>();
        check(sequences && !sequences->findChild<QTabWidget *>(), "the sequence panel has no tabs");
        if (sequences) {
            auto *menu = sequences->findChild<QComboBox *>("sequenceMenu");
            check(menu && menu->count() == sq.count() && menu->currentText() == sq.sequence(sq.current()).name, "the menu lists the sequences");
            bool timelinesButton = false;
            for (auto *b : sequences->findChildren<QPushButton *>()) timelinesButton = timelinesButton || b->text().startsWith("Timelines");
            check(!timelinesButton, "no Timelines button");
            check(!sequences->findChild<TimelineActivity *>(), "the timelines' view left the panel");
            // Edit mode at hand, the sequence's other properties and the tempo behind ⚙
            auto *editMode = sequences->findChild<QWidget *>("sequenceEditMode");
            check(editMode && editMode->isVisibleTo(sequences), "Edit mode stays in the panel");
            auto *settings = sequences->findChild<QPushButton *>("sequenceSettings");
            auto *popup = sequences->findChild<QWidget *>("sequenceSettingsPopup");
            check(settings && popup && !popup->isVisible(), "the settings, closed");
            for (const char *name : {"sequenceTracking", "sequenceBeats", "sequenceClock", "clockBpm"}) {
                auto *field = popup ? popup->findChild<QWidget *>(name) : nullptr;
                check(field && field->isVisibleTo(popup), "the sequence's properties are behind the settings button");
            }
            if (settings && popup) {
                settings->click();
                qApp->processEvents();
                check(popup->isVisible(), "the settings button opens them");
                popup->hide();
            }
            // GO and GO BACK here too, not taking the focus from the steps
            auto *goHere = sequences->findChild<QPushButton *>("sequenceGo");
            auto *backHere = sequences->findChild<QPushButton *>("sequenceBack");
            check(goHere && backHere && goHere->focusPolicy() == Qt::NoFocus, "GO and GO BACK in the panel");
            auto settle = [] { // the panel's timer tells the buttons where the sequence is
                QElapsedTimer t;
                t.start();
                while (t.elapsed() < 60) qApp->processEvents();
            };
            if (goHere && backHere) {
                sq.setCurrent(0);
                sq.setPosition(-1);
                qApp->processEvents();
                settle();
                check(goHere->isEnabled(), "GO has a next step");
                goHere->click();
                check(sq.position() == 0, "GO plays it");
                settle();
                goHere->click();
                check(sq.position() == 1, "GO plays the next");
                settle();
                backHere->click();
                check(sq.position() == 0, "GO BACK");
                sq.stopAll();
            }
            // The list follows what plays
            {
                Sequencer::Sequence longOne;
                longOne.name = "Long";
                longOne.steps.resize(60);
                sq.setCurrent(sq.add(longOne));
                qApp->processEvents();
                auto *list = sequences->findChild<QTableWidget *>();
                sq.setPosition(50);
                qApp->processEvents();
                const QRect row = list->visualItemRect(list->item(50, 1));
                check(list->viewport()->rect().contains(row), "the step played scrolls into sight");
                sq.remove(sq.current());
                sq.setCurrent(0);
                qApp->processEvents();
            }
            auto *steps = sequences->findChild<QTableWidget *>();
            auto *tracking = sequences->findChild<QWidget *>("sequenceTracking");
            auto *add = sequences->findChild<QPushButton *>("sequenceNew");
            check(steps && tracking && add, "steps, properties, buttons");
            if (menu && add) {
                const int before = sq.count();
                add->click();
                qApp->processEvents();
                check(sq.count() == before + 1 && menu->count() == before + 1 && menu->currentIndex() == sq.current(), "+ makes a sequence the menu follows");
                menu->lineEdit()->setText("Renamed");
                emit menu->lineEdit()->editingFinished();
                check(sq.sequence(sq.current()).name == "Renamed", "typing in the menu renames it");
                sq.remove(sq.current());
                sq.setCurrent(0);
                qApp->processEvents();
            }
        }
        // When a step plays, in one column: its quantize, or its timecode on a sequence with a clock
        if (sequences) {
            auto *steps = sequences->findChild<QTableWidget *>();
            auto column = [&](const QString &title) {
                for (int c = 0; c < steps->columnCount(); ++c)
                    if (steps->horizontalHeaderItem(c)->text() == title) return c;
                return -1;
            };
            check(column("Comment") < 0, "no comment column");
            const int quantize = column("Quantize"), timecode = column("Timecode");
            check(!steps->isColumnHidden(quantize) && steps->isColumnHidden(timecode), "without a clock: the quantize");
            auto clocked = sq.sequence(sq.current());
            clocked.clockParam = "playhead";
            sq.setSequence(sq.current(), clocked);
            sequences->refresh();
            check(steps->isColumnHidden(quantize) && !steps->isColumnHidden(timecode), "with a clock: the timecode, in its place");
            check(steps->horizontalHeader()->visualIndex(timecode) == steps->horizontalHeader()->visualIndex(quantize) - 1, "the two side by side");
            clocked.clockParam.clear();
            sq.setSequence(sq.current(), clocked);
            sequences->refresh();
        }
        // The library at its narrowest: every button of each bar inside it, no scroll bar under it
        window.resize(1600, 980);
        qApp->processEvents();
        auto *tabsBar = window.findChild<QTabBar *>("showWorkspaceTabs");
        auto *library = window.findChild<QWidget *>("showLibraryColumn");
        auto *librarySplit = window.findChild<QSplitter *>("showLibrarySplit");
        check(tabsBar && library && librarySplit, "the library");
        if (tabsBar && library && librarySplit) {
            librarySplit->setSizes({library->minimumWidth(), 2000});
            qApp->processEvents();
            auto *pane = window.findChild<QScrollArea *>("showLibraryScroll");
            check(pane && !pane->horizontalScrollBar()->isVisible(), "no scroll bar under the library");
            for (int tab = 0; tab < tabsBar->count(); ++tab) {
                tabsBar->setCurrentIndex(tab);
                qApp->processEvents();
                check(tabsBar->tabRect(tab).right() <= library->width(), "the tabs fit");
                for (auto *b : library->findChildren<QPushButton *>())
                    if (b->isVisible()) {
                        const QRect r(b->mapTo(library, QPoint(0, 0)), b->size());
                        check(library->rect().contains(r), "the bar fits the narrowest library");
                    }
                for (auto *list : library->findChildren<QTreeWidget *>())
                    if (list->isVisible()) check(!list->horizontalScrollBar()->isVisible(), "the list has no horizontal scroll bar");
            }
            check(library->width() <= 355, "as narrow as asked");
            tabsBar->setCurrentIndex(0);
        }
        // All the timelines folded on a line each, from the Timelines list; the same bar as the others
        auto *timelines = window.findChild<TimelineWindow *>();
        if (timelines) {
            auto *overview = timelines->listPanel()->findChild<QPushButton *>("timelinesOverview");
            auto *workspace = qobject_cast<QStackedWidget *>(timelines->workspace());
            auto *list = timelines->listPanel()->findChild<QTreeWidget *>("timelineList");
            check(overview && workspace && list, "the overview's button, in the list's bar");
            for (const char *name : {"timelineGroup", "timelineNew", "timelineDelete", "timelineDuplicate", "timelinePlay"})
                check(timelines->listPanel()->findChild<QPushButton *>(name) != nullptr, "the timelines' bar");
            if (overview && workspace && list) {
                overview->click();
                auto *activity = qobject_cast<TimelineActivity *>(workspace->currentWidget());
                check(activity != nullptr, "the button shows all the timelines");
                if (activity) {
                    activity->poll();
                    check(int(activity->rows().size()) == engine.animationCount(), "stopped ones too");
                }
                overview->click();
                check(workspace->currentWidget() == timelines->editor(), "and goes back to the editor");
                const int before = list->topLevelItemCount();
                timelines->listPanel()->findChild<QPushButton *>("timelineGroup")->click();
                qApp->processEvents();
                check(list->topLevelItemCount() == before + 1 && !engine.timelineTree().back().snapshot, "a group of timelines");
                timelines->listPanel()->findChild<QPushButton *>("timelineDelete")->click();
                qApp->processEvents();
                check(list->topLevelItemCount() == before && engine.animationCount() == 1, "− takes a selected group away, not a timeline");
            }
        }
    }

    qApp->processEvents();
    if (qEnvironmentVariableIsSet("NYXMAP_SHOW_CONTROL_SHOT")) panel.grab().save(qEnvironmentVariable("NYXMAP_SHOW_CONTROL_SHOT"));
    std::printf("show_control_ui: %d failures\n", failures);
    return failures;
}
