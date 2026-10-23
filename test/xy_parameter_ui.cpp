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
#include <QLineEdit>
#include <QMenu>
#include <QSpinBox>
#include <QTabWidget>
#include <QUndoStack>
#include <algorithm>
#include <cmath>
#include <cstdio>

int runXyParameterUiTests(Engine &engine, const QString &root)
{
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
    return failures;
}
