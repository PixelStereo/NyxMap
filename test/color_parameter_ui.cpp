#include "Engine.h"
#include "LayerInspector.h"
#include "ParamAnimPanel.h"
#include "TimelinePanel.h"
#include "ColorPicker.h"
#include "PresetEditor.h"
#include "Widgets.h"
#include "Commands.h"
#include "Osc.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QMenu>
#include <QPushButton>
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
        if (menu) for (auto *action : menu->actions()) if (action->text() == "Animate Color (RGB)") return action;
        return nullptr;
    };
    const QString start = "param/" + osc::safeName("startColor");
    const auto colors = engine.animatableColors(id);
    check(colors.size() == 5, "both gradient colors, both Color adjustments and the FX color are available");
    check(std::any_of(colors.begin(), colors.end(), [&](const auto &p) { return p.path == start; }), "startColor is a whole RGB target");
    Animation global; AnimTrack opacity; opacity.layer = id; opacity.param = "opacity"; opacity.keys = {{0, 1, 0}};
    global.tracks = {opacity};
    const int gi = engine.addAnimation(global);
    TimelineEditor timeline(&engine, &undo); timeline.setTimeline(engine.animation(gi).id); timeline.resize(1000, 350); timeline.show(); pump();
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
        undo.undo(); pump(); check(!engine.layerAnim(id, target.path, nullptr), "Undo removes the complete color animation");
        undo.redo(); pump(); check(engine.layerAnim(id, target.path, &animated) && animated.tracks.front().color, "Redo restores RGB type");
        if (!animated.tracks.empty()) {
            const auto from = animated.tracks.front().keys.front().rgb;
            animated.tracks.front().keys.back().rgb = {.1, .8, .3};
            engine.setLayerAnim(id, target.path, &animated);
            engine.controlLayerAnim(id, target.path, AnimAction::Seek, animated.duration / 2);
            std::array<double, 3> rgb;
            check(engine.animColorValue(id, target.path, &rgb) && std::abs(rgb[0] - (from[0] + .1) / 2) < 1e-4 &&
                  std::abs(rgb[1] - (from[1] + .8) / 2) < 1e-4, "local RGB animation evaluates all channels together");
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
    inspector.hide(); timeline.hide(); pump();
    std::printf("color_parameter_ui: %d failures\n", failures);
    return failures;
}
