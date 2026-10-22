#include "Engine.h"
#include "LayerInspector.h"
#include "ParamAnimPanel.h"
#include "TimelinePanel.h"
#include "SequencePanel.h"
#include "ColorPicker.h"
#include "PresetEditor.h"
#include "InteractionPanel.h"
#include "Widgets.h"
#include "Commands.h"
#include "Osc.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QDir>
#include <QJsonArray>
#include <QMenu>
#include <QPushButton>
#include <QPixmap>
#include <QSlider>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QUndoStack>
#include <algorithm>
#include <cmath>
#include <cstdio>

int runColorParameterUiTests(Engine &engine, const QString &root)
{
    theme::applyToApplication(*qApp);
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL color_parameter_ui: %s\n", why); }
    };
    auto pump = [] {
        for (int i = 0; i < 4; ++i) {
            qApp->processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    };
    auto closeMenus = [&] {
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *menu = qobject_cast<QMenu *>(widget)) menu->close();
        pump();
    };
    engine.setFadesManual(true);
    const int row = engine.addLayer("Gradient color access");
    const quint64 id = engine.layerId(row);
    QString error;
    check(engine.setLayerIsf(row, root + "/../isf/generators/Linear Gradient.fs", &error), "load the reported Linear Gradient source");
    check(engine.addEffect(row, root + "/../isf/effects/ColorCorrection.fs", &error) >= 0, "load a real FX color input");
    QUndoStack undo;
    PresetEditor presets(&engine, &undo);
    LayerInspector inspector(&engine, &undo);
    inspector.resize(700, 1000); inspector.setLayer(row); inspector.show(); pump();
    auto selectTab = [&](const QString &name) {
        for (auto *tabs : inspector.findChildren<QTabWidget *>())
            for (int i = 0; i < tabs->count(); ++i) if (tabs->tabText(i).startsWith(name)) tabs->setCurrentIndex(i);
        pump();
    };
    auto findControl = [&](const QString &path) -> QWidget * {
        QWidget *fallback = nullptr;
        for (auto *widget : inspector.findChildren<QWidget *>()) {
            if (widget->property("animColorPath").toString() != path || !widget->isVisible()) continue;
            if (qobject_cast<ColorPicker *>(widget) || qobject_cast<ColorEditor *>(widget) || qobject_cast<QPushButton *>(widget)) return widget;
            fallback = widget;
        }
        return fallback;
    };
    auto openMenu = [&](QWidget *widget) -> QMenu * {
        if (!widget) return nullptr;
        QContextMenuEvent event(QContextMenuEvent::Mouse, {4, 4}, widget->mapToGlobal(QPoint(4, 4)));
        QApplication::sendEvent(widget, &event);
        qApp->processEvents();
        return qobject_cast<QMenu *>(QApplication::activePopupWidget());
    };
    auto colorAction = [](QMenu *menu) -> QAction * {
        if (menu) for (auto *action : menu->findChildren<QAction *>()) if (action->text() == "Animate Color (RGB)") return action;
        return nullptr;
    };
    const QString start = "param/" + osc::safeName("startColor");
    const auto colors = engine.animatableColors(id);
    check(colors.size() == 5, "both gradient colors, both Color adjustments and the FX color are available");
    check(std::any_of(colors.begin(), colors.end(), [&](const auto &p) { return p.path == start; }), "startColor is a whole RGB target");
    Animation global; AnimTrack opacity; opacity.layer = id; opacity.param = "opacity"; opacity.keys = {{0, 1, 0}};
    opacity.keys[0].isCurrentValue = true;
    global.tracks = {opacity};
    const int gi = engine.addAnimation(global);
    TimelineEditor timeline(&engine, &undo); timeline.setTimeline(engine.animation(gi).id); timeline.resize(1000, 350); timeline.show(); pump();
    auto *numericMode = timeline.findChild<QPushButton *>("startFromCurrent");
    check(numericMode && numericMode->isVisible() && numericMode->isChecked() && numericMode->text().contains("CURRENT VALUE") &&
          numericMode->styleSheet().contains("#f6c453"), "numeric current-value mode has a persistent highlighted badge");
    auto inlineTarget = [&](const Engine::AnimParam &target) -> QAction * {
        const int cut = target.label.lastIndexOf(QStringLiteral(" › "));
        for (auto *button : timeline.findChildren<QPushButton *>()) {
            if (!button->menu() || !button->toolTip().contains("Choose a color")) continue;
            for (auto *category : button->menu()->actions()) if (category->menu() && category->text() == target.label.left(cut))
                for (auto *action : category->menu()->actions()) if (action->text() == target.name()) return action;
        }
        return nullptr;
    };
    for (const auto &target : colors) {
        auto *action = inlineTarget(target);
        check(action != nullptr, qPrintable("timeline category offers " + target.label));
        if (action) action->trigger();
        const auto selected = engine.animation(gi).tracks.front();
        check(selected.color && selected.param == target.path, "inline timeline entry selects a complete color track");
        selectTab(target.path.startsWith("fx/") ? "FX" : target.path.startsWith("param/") ? "Source" : "Color");
        QWidget *control = findControl(target.path);
        check(control != nullptr, qPrintable("inspector exposes " + target.path));
        // The context menu must work inside the picker too, not just on its row label.
        QWidget *click = control;
        if (control) if (auto *number = control->findChild<QDoubleSpinBox *>()) click = number;
        QMenu *menu = openMenu(click);
        QAction *create = colorAction(menu);
        check(create != nullptr, "right-click directly offers Animate Color (RGB)");
        if (create) create->trigger();
        closeMenus();
        Animation animated;
        check(engine.layerAnim(id, target.path, &animated) && animated.tracks.front().color && !animated.tracks.front().oscillator,
              "inspector creates a typed RGB animation, not an R/G/B scalar");
        bool hasLane = false;
        for (auto *editor : inspector.findChildren<ParamAnimEditor *>())
            if (editor->param() == target.path)
                for (auto *lane : editor->findChildren<ColorLane *>()) hasLane |= lane->isVisible();
        check(hasLane, "Anim tab reveals the color strip for the new local animation");
        if (!animated.tracks.empty()) {
            animated.tracks.front().keys.front().isCurrentValue = true;
            engine.setLayerAnim(id, target.path, &animated); pump();
            bool visibleMode = false;
            for (auto *editor : inspector.findChildren<ParamAnimEditor *>()) if (editor->param() == target.path)
                for (auto *button : editor->findChildren<QPushButton *>("startFromCurrent"))
                    visibleMode |= button->isVisible() && button->isChecked() && button->text().contains("CURRENT VALUE") &&
                                   button->styleSheet().contains("#f6c453");
            check(visibleMode, "RGB current-value mode stays visible in the inspector header");
        }
        undo.undo(); pump(); check(!engine.layerAnim(id, target.path, nullptr), "Undo removes the complete color animation");
        undo.redo(); pump(); check(engine.layerAnim(id, target.path, &animated) && animated.tracks.front().color, "Redo restores RGB type");
        if (!animated.tracks.empty()) {
            const auto from = animated.tracks.front().keys.front().rgb;
            animated.tracks.front().keys.front().isCurrentValue = true; // activated during playback, before a fresh capture
            animated.tracks.front().keys.back().rgb = {.1, .8, .3};
            engine.setLayerAnim(id, target.path, &animated);
            engine.controlLayerAnim(id, target.path, AnimAction::Seek, animated.duration / 2);
            std::array<double, 3> rgb{};
            check(engine.animColorValue(id, target.path, &rgb) && std::abs(rgb[0] - (from[0] + .1) / 2) < 1e-4 &&
                  std::abs(rgb[1] - (from[1] + .8) / 2) < 1e-4, "local RGB animation evaluates all channels together");
            pump();
            for (auto *editor : inspector.findChildren<ParamAnimEditor *>()) if (editor->param() == target.path) {
                editor->poll();
                for (auto *lane : editor->findChildren<ColorLane *>()) if (lane->isVisible()) {
                    const QImage preview = lane->grab().toImage();
                    const QColor midpoint = preview.pixelColor(preview.width() / 2 + 4, 48); // beside the playhead
                    check(std::abs(midpoint.redF() - rgb[0]) < .02 && std::abs(midpoint.greenF() - rgb[1]) < .02 &&
                          std::abs(midpoint.blueF() - rgb[2]) < .02,
                          "mid-play current-value preview matches the engine without feeding back its moving output");
                }
            }
        }
        paramanim::remove(&engine, &undo, id, target.path); pump();
    }
    // + Animate must offer the same whole colors in their original categories.
    selectTab("Anim");
    bool foundStart = false;
    for (auto *button : inspector.findChildren<QPushButton *>()) if (button->text() == "+ Animate" && button->isVisible() && button->menu()) {
        auto *menu = button->menu(); emit menu->aboutToShow();
        for (auto *category : menu->actions()) if (category->menu() && category->text() == "Source")
            for (auto *action : category->menu()->actions()) if (action->text() == "startColor (RGB)") foundStart = true;
    }
    check(foundStart, "+ Animate offers startColor (RGB) beside its channel entries");
    // Grouping existing component animations must be reversible and must leave Alpha alone.
    paramanim::animate(&engine, &undo, id, start + "/r", -1);
    paramanim::animate(&engine, &undo, id, start + "/g", -1);
    paramanim::animate(&engine, &undo, id, start + "/a", -1);
    paramanim::animate(&engine, &undo, id, start, -1); pump();
    check(engine.layerAnim(id, start, nullptr) && !engine.layerAnim(id, start + "/r", nullptr) &&
          !engine.layerAnim(id, start + "/g", nullptr) && engine.layerAnim(id, start + "/a", nullptr), "RGB replaces competing components while preserving Alpha");
    undo.undo(); pump();
    check(!engine.layerAnim(id, start, nullptr) && engine.layerAnim(id, start + "/r", nullptr) && engine.layerAnim(id, start + "/g", nullptr),
          "one Undo restores the previous component animations");
    undo.redo(); pump();
    presets.addToNew(id, {start});
    const auto preset = engine.preset(engine.presetCount() - 1);
    check(preset.values.size() == 3 && preset.animations.size() == 1 && preset.animations[0].tracks[0].color,
          "a color preset stores RGB values and one color driver");
    paramanim::animate(&engine, &undo, id, start + "/r", -1);
    paramanim::animate(&engine, &undo, id, start + "/g", -1);
    const auto beforeRecall = engine.layerAnims(id);
    undo.push(new cmd::RecallPreset(&engine, preset.id)); pump();
    check(engine.layerAnim(id, start, nullptr) && !engine.layerAnim(id, start + "/r", nullptr) &&
          !engine.layerAnim(id, start + "/g", nullptr) && engine.layerAnim(id, start + "/a", nullptr),
          "recalling a color preset replaces component drivers and preserves Alpha");
    undo.undo(); pump();
    check(!engine.layerAnim(id, start, nullptr) && engine.layerAnim(id, start + "/r", nullptr) &&
          engine.layerAnim(id, start + "/g", nullptr) && engine.layerAnims(id).size() == beforeRecall.size(),
          "Undo of a color preset restores the previous component drivers");
    undo.redo(); pump();
    check(engine.layerAnim(id, start, nullptr) && !engine.layerAnim(id, start + "/r", nullptr), "Redo restores the single color driver");
    Engine::Preset component;
    component.name = "Red channel";
    component.fade = 0;
    component.animations = {engine.makeLayerAnim(id, start + "/r", -1)};
    const int ci = engine.addPreset(component);
    std::array<double, 3> beforeComponent;
    engine.animColorValue(id, start, &beforeComponent);
    undo.push(new cmd::RecallPreset(&engine, engine.preset(ci).id)); pump();
    check(!engine.layerAnim(id, start, nullptr) && engine.layerAnim(id, start + "/r", nullptr) &&
          engine.layerAnim(id, start + "/a", nullptr), "a component preset replaces a competing RGB driver");
    undo.undo(); pump();
    std::array<double, 3> afterComponent;
    check(engine.layerAnim(id, start, nullptr) && !engine.layerAnim(id, start + "/r", nullptr) &&
          engine.animColorValue(id, start, &afterComponent) && beforeComponent == afterComponent,
          "Undo of a component preset restores the previous RGB driver and its complete output");
    const auto capture = engine.captureLayers();
    for (const auto &value : capture) if (value.toObject().value("id").toString() == QString::number(id)) {
        const auto params = value.toObject().value("params").toObject();
        check(!params.contains(start + "/r") && !params.contains(start + "/g") && !params.contains(start + "/b"),
              "snapshot values do not compete with their color animation");
    }
    QTemporaryDir files;
    check(engine.saveProject(files.filePath("colors.pinol"), {}, &error), "save local color animations");
    inspector.hide(); timeline.hide(); pump();

    // Reproduce changing time/pass widths in both layouts using the real
    // engine transport. Polling must neither resize the editor nor seek it.
    const int transportRow = engine.addLayer("Transport geometry");
    const quint64 transportLayer = engine.layerId(transportRow);
    Animation transport; transport.duration = 36000; transport.loop = AnimLoop::PingPong;
    AnimTrack transportTrack; transportTrack.layer = transportLayer; transportTrack.param = "opacity";
    transportTrack.keys = {{0, 0, 0}, {36000, 1, 0}}; transport.tracks = {transportTrack};
    const int transportIndex = engine.addAnimation(transport);
    const quint64 transportId = engine.animation(transportIndex).id;
    engine.setLayerAnim(transportLayer, "opacity", &transport);
    engine.controlLayerAnim(transportLayer, "opacity", AnimAction::Pause); // layer animations start enabled and playing
    TimelineEditor wide(&engine, &undo); wide.setTimeline(transportId); wide.resize(1000, 420);
    ParamAnimEditor narrow(&engine, &undo, transportLayer, "opacity", AnimEditor::Layout::Stacked); narrow.resize(420, 520);
    auto checkTransport = [&](AnimEditor &editor, auto seek, auto read, const QString &captureName) {
        editor.show(); seek(9.99); editor.poll(); pump();
        auto *slider = editor.findChild<QSlider *>("animationPosition");
        auto *elapsed = editor.findChild<QLabel *>("animationElapsed");
        check(slider && elapsed, "both animation layouts expose a playback slider and elapsed readout");
        if (!slider || !elapsed) return;
        check(elapsed->geometry().top() >= slider->geometry().bottom(), "time digits are below the playback slider");
        const QSize preferred = editor.sizeHint(), minimum = editor.minimumSizeHint(), actual = editor.size();
        const QRect digits = elapsed->geometry();
        for (double clock : {10., 59.99, 60., 5999.99, 6000., 99999999.99}) {
            seek(clock); editor.poll(); pump();
            check(editor.sizeHint() == preferred && editor.minimumSizeHint() == minimum && editor.size() == actual &&
                  elapsed->geometry() == digits, "time and pass digit changes leave editor and readout geometry unchanged");
            check(std::abs(read().clock - clock) < 1e-5, "playback polling never feeds a rounded slider value back into the engine");
        }
        seek(transport.duration + 100); editor.poll(); // the backwards pass
        const int undoCount = undo.count();
        slider->setValue(slider->maximum() / 2);
        const Animation sought = read();
        check(std::abs(sought.position() - transport.duration / 2) < 1e-5 && sought.backwardsAt(sought.clock) &&
              sought.state == AnimState::Paused && undo.count() == undoCount,
              "scrubbing seeks within the backwards pass without resuming playback or creating an edit");
        slider->setSliderDown(true);
        slider->setValue(slider->maximum() / 4);
        seek(123); editor.poll();
        check(slider->value() == slider->maximum() / 4, "playback polling does not move the handle during a drag");
        slider->setSliderDown(false); editor.poll(); pump();
        const QString captures = qEnvironmentVariable("PINOL_UI_CAPTURE_DIR");
        if (!captures.isEmpty()) {
            seek(99999999.99); editor.poll(); pump();
            check(QDir().mkpath(captures), "create CI transport screenshot directory");
            check(editor.grab().save(QDir(captures).filePath(captureName + ".png")), "save CI transport screenshot");
        }
        editor.hide(); pump();
    };
    checkTransport(wide, [&](double clock) { engine.controlAnimation(transportId, AnimAction::Seek, clock); },
                   [&] { return engine.animation(engine.indexOfAnimation(transportId)); }, "timeline");
    checkTransport(narrow, [&](double clock) { engine.controlLayerAnim(transportLayer, "opacity", AnimAction::Seek, clock); },
                   [&] { Animation a; engine.layerAnim(transportLayer, "opacity", &a); return a; }, "parameter-animation");
    engine.removeAnimation(engine.indexOfAnimation(transportId)); engine.removeLayer(transportRow);
    check(engine.loadProject(files.filePath("colors.pinol"), nullptr, &error), "reload local color animations");
    Animation restored;
    check(engine.layerAnim(id, start, &restored) && restored.tracks[0].color, "project reload preserves grouped local RGB animation");
    const int index = engine.indexOfId(id);
    check(engine.setLayerText(index), "switch to native text source"); inspector.setLayer(index); inspector.rebuild(); inspector.show(); pump();
    const auto textColors = engine.animatableColors(id);
    int offered = 0;
    selectTab("Source");
    for (const auto &target : textColors) if (target.path.startsWith("text/")) {
        auto *widget = findControl(target.path);
        check(widget != nullptr, "text fill/outline/shadow color buttons are animation targets");
        auto *menu = openMenu(widget); if (colorAction(menu)) ++offered; closeMenus();
    }
    check(offered == 3, "all native text colors offer grouped RGB animation");
    engine.layer(index)->locked = true; inspector.rebuild(); pump(); selectTab("Source");
    for (auto *widget : inspector.findChildren<QWidget *>()) if (widget->property("animColorPath").toString() == "text/color")
        check(!widget->isEnabled(), "locked text color cannot start an animation");
    engine.layer(index)->locked = false;
    check(engine.setLayerIsf(index, root + "/../isf/generators/Clouds.fs", &error), "load the reported Clouds source");
    inspector.rebuild(); pump(); selectTab("Source");
    InteractionPanel interactions(&engine, &undo);
    const QString shadow = "param/" + osc::safeName("colorA"), wind = "param/" + osc::safeName("direction");
    auto submenu = [](QMenu *menu, const QString &name) -> QMenu * {
        if (menu) for (auto *action : menu->actions()) if (action->text() == name) return action->menu();
        return nullptr;
    };
    QWidget *shadowControl = findControl(shadow);
    check(shadowControl && shadowControl->property("valuePaths").toStringList() ==
          QStringList{shadow + "/r", shadow + "/g", shadow + "/b", shadow + "/a"},
          "Clouds color widgets register only their real RGBA components");
    auto *cloudMenu = openMenu(shadowControl);
    check(cloudMenu && cloudMenu->actions().size() == 4 && submenu(cloudMenu, "Preset") && submenu(cloudMenu, "Snapshot") &&
          submenu(cloudMenu, "Animate") && submenu(cloudMenu, "Copy OSC Address"), "parameter context menu has the four requested groups");
    auto *animateMenu = submenu(cloudMenu, "Animate");
    check(submenu(animateMenu, "Layer Animation") && submenu(animateMenu, "Timeline") && submenu(animateMenu, "Interaction"),
          "Animate offers local animation, timeline and interaction protocols");
    bool hasCoordinates = false;
    if (cloudMenu) for (auto *action : cloudMenu->findChildren<QAction *>())
        hasCoordinates |= action->text() == "x" || action->text() == "y";
    check(colorAction(cloudMenu) && !hasCoordinates, "Clouds color menu never offers point coordinates");
    auto *timelineMenu = submenu(animateMenu, "Timeline");
    const int timelineCount = engine.animationCount();
    if (timelineMenu && !timelineMenu->actions().isEmpty()) timelineMenu->actions().first()->trigger();
    check(engine.animationCount() == timelineCount + 1, "right-click creates a timeline directly from the color");
    const auto newTimeline = engine.animation(engine.animationCount() - 1);
    check(newTimeline.tracks.size() == 1 && newTimeline.tracks[0].color && newTimeline.tracks[0].param == shadow,
          "new timeline receives a whole RGB color track");
    closeMenus(); undo.undo(); pump();
    check(engine.indexOfAnimation(newTimeline.id) < 0, "new timeline from a parameter is undoable");
    undo.redo(); pump();
    QWidget *windX = nullptr, *windY = nullptr;
    for (auto *widget : inspector.findChildren<QDoubleSpinBox *>()) if (widget->isVisible()) {
        const auto paths = widget->property("valuePaths").toStringList();
        if (paths == QStringList{wind + "/x"}) windX = widget;
        if (paths == QStringList{wind + "/y"}) windY = widget;
    }
    check(windX && windY, "Wind X and Y register separate typed targets");
    auto *windMenu = openMenu(windX);
    check(windMenu && !colorAction(windMenu), "Wind X never offers RGB color animation");
    if (windMenu) for (auto *action : windMenu->actions()) if (action->text() == "Copy OSC Address") action->trigger();
    check(QApplication::clipboard()->text() == OscNamespace::addressOf(&engine, id, wind + "/x"),
          "Copy OSC Address uses exactly the clicked component");
    auto *windTimeline = submenu(submenu(windMenu, "Animate"), "Timeline");
    auto addWind = [&](QMenu *menu) {
        if (menu) for (auto *action : menu->actions()) if (action->text() == newTimeline.name) { action->trigger(); return; }
    };
    addWind(windTimeline); closeMenus(); pump();
    const int newIndex = engine.indexOfAnimation(newTimeline.id);
    auto tracks = engine.animation(newIndex).tracks;
    check(tracks.size() == 2 && tracks[0].color && !tracks[1].color && tracks[1].param == wind + "/x",
          "an existing timeline keeps its color track and adds only the clicked Wind X parameter");
    windMenu = openMenu(windX); addWind(submenu(submenu(windMenu, "Animate"), "Timeline")); closeMenus();
    check(engine.animation(newIndex).tracks.size() == 2, "adding the same timeline parameter twice does not duplicate its track");
    undo.undo(); pump();
    check(engine.animation(newIndex).tracks.size() == 1, "adding a parameter to an existing timeline is one undo step");
    Engine::Preset cuePreset; cuePreset.name = "Cue preset"; cuePreset.values = {engine.presetValue(id, wind + "/x")};
    const int cuePresetIndex = engine.addPreset(cuePreset);
    auto &sequencer = engine.sequencer(); sequencer.clear();
    Sequencer::Sequence caller; caller.name = "Caller"; caller.steps.resize(1);
    Sequencer::Sequence child; child.name = "Child"; child.steps.resize(1);
    sequencer.add(caller); const int childIndex = sequencer.add(child); sequencer.setCurrent(0);
    SequenceWindow sequences(&engine);
    QMenu cueMenu; sequences.fillStepMenu(&cueMenu, 0);
    auto *presetMenu = submenu(&cueMenu, "Preset of this step");
    if (presetMenu) for (auto *action : presetMenu->actions()) if (action->text() == cuePreset.name) action->trigger();
    check(sequencer.sequence(0).steps[0].preset == engine.preset(cuePresetIndex).id,
          "step context menu chooses a preset target");
    QMenu sequenceMenu; sequences.fillStepMenu(&sequenceMenu, 0);
    auto *playSequence = submenu(&sequenceMenu, "Play sequence");
    if (playSequence) for (auto *action : playSequence->actions()) if (action->text() == "Child") action->trigger();
    check(sequencer.sequence(0).steps[0].sequence == sequencer.sequenceId(childIndex) && !sequencer.sequence(0).steps[0].preset,
          "step context menu plays another sequence and clears the previous preset target");
    sequencer.setCurrent(childIndex);
    QMenu cyclicMenu; sequences.fillStepMenu(&cyclicMenu, 0);
    auto *cyclic = submenu(&cyclicMenu, "Play sequence");
    bool disabled = false;
    if (cyclic) for (auto *action : cyclic->actions()) if (action->text() == "Caller") disabled = !action->isEnabled();
    check(disabled, "sequence choices disable circular calls before they can be added");
    sequencer.clear();
    const quint64 interactionId = engine.addInteraction(Interaction::made(InteractionSource::Osc));
    const QString master = QStringLiteral("interaction/%1/master").arg(interactionId);
    QMenu masterMenu;
    TimelineWindow::fillParameterMenu(&masterMenu, &engine, &undo, 0, {master}, &inspector);
    auto *masterTimeline = submenu(submenu(&masterMenu, "Animate"), "Timeline");
    if (masterTimeline && !masterTimeline->actions().isEmpty()) masterTimeline->actions().first()->trigger();
    const auto masterTracks = engine.animation(engine.animationCount() - 1).tracks;
    check(masterTracks.size() == 1 && masterTracks[0].layer == 0 && masterTracks[0].param == master,
          "interaction settings can also be added to a timeline from their parameter menu");
    undo.undo(); engine.removeInteraction(interactionId);
    inspector.hide(); timeline.hide(); pump();
    std::printf("color_parameter_ui: %d failures\n", failures);
    return failures;
}
