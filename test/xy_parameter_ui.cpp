#include "Engine.h"
#include "LayerInspector.h"
#include "ParamAnimPanel.h"
#include "TimelinePanel.h"
#include "PresetEditor.h"
#include "InteractionPanel.h"
#include "Widgets.h"
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QDir>
#include <QCheckBox>
#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QMenu>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QUndoStack>
#include <algorithm>
#include <cmath>
#include <cstdio>

int runXyParameterUiTests(Engine &engine, const QString &root)
{
    theme::applyToApplication(*qApp);
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL xy_parameter_ui: %s\n", why); }
    };
    auto pump = [] {
        for (int k = 0; k < 4; ++k) {
            qApp->processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    };
    engine.setFadesManual(true);
    const int li = engine.addLayer("XY UI");
    const quint64 layer = engine.layerId(li);
    QString error;
    if (!engine.setLayerIsf(li, root + "/../isf/generators/SolidColor.fs", &error)) {
        check(false, "load source fixture"); return failures;
    }
    const int fx = engine.addEffect(li, root + "/../isf/effects/Layer Position.fs", &error);
    if (fx < 0) { check(false, "load Layer Position FX"); return failures; }
    const QString point = engine.shaderPrefix(layer, fx) + "param/offset";
    QUndoStack undo;
    PresetEditor presets(&engine, &undo);
    LayerInspector inspector(&engine, &undo);
    inspector.resize(700, 1000); inspector.setLayer(li); inspector.show(); pump();
    for (auto *tabs : inspector.findChildren<QTabWidget *>())
        for (int k = 0; k < tabs->count(); ++k) if (tabs->tabText(k).startsWith("FX")) tabs->setCurrentIndex(k);
    pump();
    QWidget *group = nullptr, *xOnly = nullptr;
    for (auto *widget : inspector.findChildren<QWidget *>()) {
        if (widget->property("animPointPath").toString() == point && widget->isVisible()) group = widget;
        if (widget->property("valuePaths").toStringList() == QStringList{point + "/x"} && widget->isVisible()) xOnly = widget;
    }
    check(group && xOnly, "inspector exposes XY together and X separately");
    if (group) {
        QContextMenuEvent event(QContextMenuEvent::Mouse, {4, 4}, group->mapToGlobal(QPoint(4, 4)));
        QApplication::sendEvent(group, &event); pump();
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        check(menu != nullptr, "XY row opens a parameter context menu");
        if (menu) {
            bool sine = false, rgb = false;
            for (auto *action : menu->findChildren<QAction *>()) {
                sine |= action->text() == "Sine";
                rgb |= action->text() == "Animate Color (RGB)";
            }
            check(sine && !rgb, "XY offers two-axis animation without unrelated RGB actions");
            menu->close(); pump();
        }
    }
    QMenu timelineMenu;
    TimelineWindow::fillValueMenu(&timelineMenu, &engine, &undo, layer, {point}, &inspector);
    timelineMenu.actions().front()->trigger();
    const auto timelineState = engine.animation(engine.animationCount() - 1);
    check(timelineState.tracks.size() == 1 && timelineState.tracks[0].isPoint(), "parameter menu adds one grouped XY timeline track");
    TimelineEditor timeline(&engine, &undo);
    timeline.setTimeline(timelineState.id); timeline.resize(1000, 420); timeline.show(); pump();
    auto *timelineAxis = timeline.findChild<QComboBox *>("xyAnimationAxis");
    check(timelineAxis && timelineAxis->isVisible(), "global timeline exposes both component curves in one track");
    paramanim::animate(&engine, &undo, layer, point, int(AnimWave::Sine));
    Animation local;
    check(engine.layerAnim(layer, point, &local) && local.tracks[0].isPoint(), "one local animation owns both axes");
    ParamAnimEditor editor(&engine, &undo, layer, point, AnimEditor::Layout::Side);
    editor.resize(1000, 420); editor.show(); pump();
    auto *showPath = editor.findChild<QPushButton *>("showXyPath");
    check(showPath && showPath->isVisible(), "grouped animation offers an XY path preview");
    if (showPath) showPath->click();
    pump();
    auto *axisBox = editor.findChild<QComboBox *>("xyAnimationAxis");
    check(axisBox && axisBox->isVisible(), "grouped editor has an explicit X/Y selector");
    if (axisBox) {
        axisBox->setCurrentIndex(1);
        QMetaObject::invokeMethod(axisBox, "activated", Qt::DirectConnection, Q_ARG(int, 1)); pump();
        auto *phase = editor.findChild<QDoubleSpinBox *>("animationPhase");
        check(phase && std::abs(phase->value() - .25) < 1e-6, "Y editor shows its own phase");
        if (phase) phase->setValue(.5);
        check(engine.layerAnim(layer, point, &local) && std::abs(local.tracks[0].axes[1].phase - .5) < 1e-6 &&
              std::abs(local.tracks[0].axes[0].phase) < 1e-6, "editing Y phase leaves X unchanged");
        undo.undo(); pump();
        check(engine.layerAnim(layer, point, &local) && std::abs(local.tracks[0].axes[1].phase - .25) < 1e-6,
              "Undo restores the Y phase as part of the grouped animation");
        auto *preview = editor.findChild<XyPathPreview *>();
        check(preview && preview->isVisible(), "XY path stays open across axis selection and Undo");
    }
    presets.addToNew(layer, {point});
    const auto preset = engine.preset(engine.presetCount() - 1);
    check(preset.indexOf(layer, point + "/x") >= 0 && preset.indexOf(layer, point + "/y") >= 0 &&
          preset.animations.size() == 1 && preset.animations[0].tracks[0].isPoint(), "preset captures XY values and their grouped animation");
    presets.addToNew(layer, {point + "/x"});
    const auto xPreset = engine.preset(engine.presetCount() - 1);
    check(xPreset.indexOf(layer, point + "/y") < 0 && xPreset.animations.size() == 1 &&
          xPreset.animations[0].tracks[0].param == point + "/x" && !xPreset.animations[0].tracks[0].isPoint(),
          "X-only preset captures only the X animation");
    paramanim::animate(&engine, &undo, layer, point + "/x", int(AnimWave::Triangle));
    check(engine.layerAnim(layer, point, &local) && !local.tracks[0].axes[0].enabled && local.tracks[0].axes[1].enabled &&
          engine.layerAnim(layer, point + "/x", nullptr), "adding a separate X animation preserves grouped Y");
    undo.undo(); pump();
    check(engine.layerAnim(layer, point, &local) && local.tracks[0].axes[0].enabled && !engine.layerAnim(layer, point + "/x", nullptr),
          "Undo restores the two-axis binding");

    InteractionPanel interactions(&engine, &undo);
    interactions.resize(600, 900); interactions.show();
    interactions.learnFor(layer, point, InteractionSource::Osc); pump();
    auto *inputAxis = interactions.findChild<QComboBox *>("xyInteractionAxis");
    check(inputAxis && inputAxis->isVisible(), "interaction editor exposes two input chains for an XY target");
    if (inputAxis) {
        inputAxis->setCurrentIndex(1);
        QMetaObject::invokeMethod(inputAxis, "activated", Qt::DirectConnection, Q_ARG(int, 1)); pump();
        auto *argument = interactions.findChild<QSpinBox *>("interactionOscArgument");
        check(argument && argument->value() == 1, "Y input starts on the second OSC argument");
        auto *address = interactions.findChild<QLineEdit *>("interactionOscAddress");
        if (address) {
            address->setText("/independent/y");
            QMetaObject::invokeMethod(address, "editingFinished", Qt::DirectConnection); pump();
        }
        const auto list = engine.interactions();
        check(list.size() == 1 && !list[0].xyInputs.empty() && list[0].xyInputs[0].oscAddress == "/independent/y" &&
              list[0].oscAddress.isEmpty(), "editing Y source does not replace X source");
    }
    engine.setInteractionLearn(false);
    engine.setInteractions({});

    // Compare the preview to actual engine output, especially after elapsed
    // wave time diverges from the position within a loop or ping-pong pass.
    auto wave = engine.makeLayerAnim(layer, point, int(AnimWave::Sine));
    wave.duration = 1;
    for (auto &axis : wave.tracks[0].axes) { axis.period = 3.7; axis.center = .5; axis.amplitude = .25; }
    engine.setLayerAnim(layer, point, &wave);
    ParamAnimEditor pathEditor(&engine, &undo, layer, point, AnimEditor::Layout::Side);
    pathEditor.resize(1000, 560); pathEditor.show(); pump();
    auto *pathToggle = pathEditor.findChild<QPushButton *>("showXyPath");
    if (pathToggle) pathToggle->click();
    pump();
    auto previewMatchesOutput = [&](AnimEditor &view) {
        pump();
        view.poll();
        auto *preview = view.findChild<XyPathPreview *>();
        Engine::AnimationPlayback playback;
        if (!preview || !engine.sampleAnimation(0, layer, point, &playback)) return false;
        const auto value = preview->valueAt(playback.position);
        return std::abs(value[0] - engine.parameterValue(layer, point + "/x").toDouble()) < 1e-6 &&
               std::abs(value[1] - engine.parameterValue(layer, point + "/y").toDouble()) < 1e-6;
    };
    engine.controlLayerAnim(layer, point, AnimAction::Play);
    engine.advanceFades(2.4);
    check(previewMatchesOutput(pathEditor), "XY path uses continuous wave time across loop boundaries");
    engine.controlLayerAnim(layer, point, AnimAction::LoopMode, 0, AnimLoop::PingPong);
    engine.advanceFades(1.1);
    Engine::AnimationPlayback playback;
    check(engine.sampleAnimation(0, layer, point, &playback) && playback.backwards && std::abs(playback.played - 3.5) < 1e-6,
          "telemetry retains wave time and direction after changing loop mode");
    check(previewMatchesOutput(pathEditor), "XY path matches actual output on a backwards pass with a wave offset");
    auto clickPath = [&](XyPathPreview *preview) {
        if (!preview) return;
        const QPointF pos = preview->rect().center();
        const QPointF global = preview->mapToGlobal(pos.toPoint());
        QMouseEvent down(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent up(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(preview, &down); QApplication::sendEvent(preview, &up); pump();
    };
    clickPath(pathEditor.findChild<XyPathPreview *>());
    check(engine.layerAnim(layer, point, &local) && local.clock >= 1 && local.clock < 2,
          "seeking along the path stays in the current backwards pass");
    check(previewMatchesOutput(pathEditor), "path seeking updates both coordinates through the shared transport");
    const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR");
    if (!captures.isEmpty()) {
        check(QDir().mkpath(captures), "create XY screenshot directory");
        check(pathEditor.grab().save(QDir(captures).filePath("xy-path-wave.png")), "save XY wave preview in CI");
    }
    engine.controlLayerAnim(layer, point, AnimAction::Stop);
    wave.tracks[0].axes[0].enabled = false;
    auto &y = wave.tracks[0].axes[1];
    y.oscillator = false; y.keys = {{0, 0, 0}, {1, .9, 0}}; y.keys[0].isCurrentValue = true;
    engine.setLayerAnim(layer, point, &wave);
    engine.setParameter(layer, point + "/x", .22); engine.setParameter(layer, point + "/y", .31);
    engine.controlLayerAnim(layer, point, AnimAction::Play); engine.advanceFades(.4); pump();
    check(previewMatchesOutput(pathEditor), "XY path uses captured starts and holds a disabled axis at its live value");
    engine.setParameter(layer, point + "/x", .8);
    check(previewMatchesOutput(pathEditor), "manual X changes update the preview without recapturing the Y start");
    engine.controlLayerAnim(layer, point, AnimAction::Stop);

    auto keys = engine.makeLayerAnim(layer, point, -1);
    keys.duration = 1;
    for (auto &axis : keys.tracks[0].axes) axis.keys = {{0, 0, 0}, {1, 1, 0}};
    const int previewTimeline = engine.addAnimation(keys);
    TimelineEditor timelinePath(&engine, &undo);
    timelinePath.setTimeline(engine.animation(previewTimeline).id);
    timelinePath.resize(1000, 560); timelinePath.show(); pump();
    auto *timelinePathToggle = timelinePath.findChild<QPushButton *>("showXyPath");
    check(timelinePathToggle && timelinePathToggle->isVisible(), "global timelines offer the same XY path view");
    if (timelinePathToggle) timelinePathToggle->click();
    pump();
    const auto definition = engine.animation(previewTimeline);
    const int undoCount = undo.count();
    clickPath(timelinePath.findChild<XyPathPreview *>());
    const auto sought = engine.animation(previewTimeline);
    check(sought.state == AnimState::Paused && sought.clock > .1 && sought.clock < .9 &&
          std::abs(engine.parameterValue(layer, point + "/x").toDouble() - sought.position()) < 1e-6 &&
          std::abs(engine.parameterValue(layer, point + "/y").toDouble() - sought.position()) < 1e-6,
          "clicking a global XY path seeks the pair instead of writing unrelated scalar values");
    check(sameAnimation(definition, sought) && undo.count() == undoCount, "path scrubbing preserves the curves and Undo history");
    if (!captures.isEmpty())
        check(timelinePath.grab().save(QDir(captures).filePath("xy-path-timeline.png")), "save global XY path preview in CI");
    auto *targetMenu = timelinePath.findChild<QPushButton *>("timelineParameter");
    QAction *scalarTarget = nullptr;
    if (targetMenu && targetMenu->menu())
        for (auto *action : targetMenu->menu()->actions())
            if (action->text() == QLatin1String("Opacity") && !action->menu()) scalarTarget = action;
    check(scalarTarget != nullptr, "timeline target menu offers a scalar target beside XY");
    if (scalarTarget) scalarTarget->trigger();
    pump();
    const auto *scalarPreview = timelinePath.findChild<XyPathPreview *>();
    const auto *scalarToggle = timelinePath.findChild<QPushButton *>("showXyPath");
    check(scalarPreview && !scalarPreview->isVisible() && scalarToggle && !scalarToggle->isVisible(),
          "switching to a scalar target hides XY-only controls");

    // The FX row carries its speed, its mask and its channels; building it pushes nothing to undo
    inspector.setLayer(li); pump();
    for (auto *tabs : inspector.findChildren<QTabWidget *>())
        for (int k = 0; k < tabs->count(); ++k) if (tabs->tabText(k).startsWith("FX")) tabs->setCurrentIndex(k);
    pump();
    const int steps = undo.count();
    auto *list = inspector.findChild<QListWidget *>("fxChainList");
    QWidget *row = list && list->count() ? list->itemWidget(list->item(0)) : nullptr;
    auto *speed = row ? row->findChild<QDoubleSpinBox *>("fxSpeed") : nullptr;
    auto *mask = row ? row->findChild<QToolButton *>("fxMask") : nullptr;
    auto *red = row ? row->findChild<QToolButton *>("fxChannelR") : nullptr;
    check(speed && mask && red, "FX row has its speed, mask and channels");
    check(undo.count() == steps, "building the FX rows is not an edit");
    // Its switch, under the row's controls, still enables and disables it
    if (list && list->count()) {
        QListWidgetItem *item = list->item(0);
        const bool was = item->checkState() == Qt::Checked;
        QStyleOptionViewItem opt;
        opt.initFrom(list->viewport());
        opt.rect = list->visualItemRect(item);
        opt.features |= QStyleOptionViewItem::HasCheckIndicator;
        const QRect box = list->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &opt, list);
        const QPoint at = box.isValid() ? box.center() : opt.rect.topLeft() + QPoint(12, opt.rect.height() / 2);
        // As the mouse would: to the widget under the point (the row's controls, if they covered it)
        auto click = [&] {
            QWidget *target = list->viewport()->childAt(at);
            if (!target) target = list->viewport();
            const QPoint local = target->mapFrom(list->viewport(), at);
            for (QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
                QMouseEvent e(type, local, target->mapToGlobal(local), Qt::LeftButton,
                              type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, {});
                QApplication::sendEvent(target, &e);
            }
            pump();
        };
        QWidget *target = list->viewport()->childAt(at);
        check(!target || !row || (target != row && !row->isAncestorOf(target)), "the row's controls leave its switch to the list");
        const int before = undo.count();
        click();
        check(undo.count() == before + 1, "the switch's click is one undo step");
        list = inspector.findChild<QListWidget *>("fxChainList");
        item = list && list->count() ? list->item(0) : nullptr;
        check(item && (item->checkState() == Qt::Checked) != was, "a click on the row's switch toggles the FX");
        click();
    }
    // Layer Position never reads TIME: no speed (its place kept). Its offset is animated (above): ∿ on its row
    check(speed && speed->isHidden(), "an FX without TIME shows no speed");
    auto *animated = row ? row->findChild<QToolButton *>("fxAnimated") : nullptr;
    check(animated && !animated->isHidden(), "an FX whose number is animated shows ∿ on its row");
    if (red) {
        red->click(); pump();
        check(!engine.parameterValue(layer, engine.shaderPrefix(layer, fx) + "channel/r").toBool(true),
              "row R button leaves the red channel out");
    }
    if (mask) {
        mask->click(); pump();
        auto *pop = qobject_cast<QFrame *>(QApplication::activePopupWidget());
        check(pop && pop->findChild<QComboBox *>("fxMaskLayer") && pop->findChild<QComboBox *>("fxMaskStage") &&
                  pop->findChild<QCheckBox *>("fxMaskOnly"),
              "mask chip opens the mask's layer, stage, invert and mask-only settings");
        if (!captures.isEmpty()) inspector.grab().save(QDir(captures).filePath("fx-rows.png"));
        if (pop) pop->close();
        pump();
    }
    // A shader that reads TIME: its speed on the row, edited there
    const int timed = engine.addEffect(li, root + "/../isf/effects/Zoom Volumetric.fs", &error);
    check(timed >= 0, "load Zoom Volumetric FX");
    inspector.setLayer(li); pump();
    list = inspector.findChild<QListWidget *>("fxChainList");
    row = list && list->count() > timed ? list->itemWidget(list->item(timed)) : nullptr;
    speed = row ? row->findChild<QDoubleSpinBox *>("fxSpeed") : nullptr;
    check(speed && !speed->isHidden(), "an FX reading TIME shows its speed");
    if (speed && timed >= 0) {
        speed->setValue(0.5); pump();
        check(std::abs(engine.parameterValue(layer, engine.shaderPrefix(layer, timed) + "speed").toDouble() - 0.5) < 1e-6,
              "row speed sets the FX's speed");
        auto *idle = row->findChild<QToolButton *>("fxAnimated");
        check(idle && idle->isHidden(), "no ∿ on an FX with nothing animated");
    }
    return failures;
}
