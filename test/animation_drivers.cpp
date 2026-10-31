#include "AnimationDrivers.h"
#include "Engine.h"
#include "InteractionPanel.h"
#include "ParamAnimPanel.h"
#include "PresetEditor.h"
#include "Widgets.h"
#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QUndoStack>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void check(bool value, const char *why)
{
    if (!value) throw std::runtime_error(why);
}
static bool near(double a, double b) { return std::abs(a - b) < 1e-5; }
int main(int argc, char **argv)
{
    QSurfaceFormat f;
    f.setVersion(3, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(f);
    QApplication app(argc, argv);
    Engine e;
    QString error;
    if (!e.initialize(&error)) {
        std::cerr << error.toStdString();
        return 1;
    }
    e.setCompositionSize(QSize(64, 32));
    e.setFadesManual(true);
    try {
        const int index = e.addLayer("Driver tests");
        const auto id = e.layerId(index);
        check(e.setLayerText(index), "text fixture");
        auto a = e.makeLayerAnim(id, "opacity", -1);
        a.tracks.front().keys = {{0, .2, 0}, {4, .6, 0}};
        auto math = InteractionStage::made(InteractionFilter::Math);
        math.op = MathOp::Multiply;
        math.a = 2;
        auto clamp = InteractionStage::made(InteractionFilter::Clamp);
        clamp.a = 0;
        clamp.b = .7;
        a.tracks.front().dataFilters = Interaction::filtersToJson({math, clamp});
        e.setLayerAnim(id, "opacity", &a);
        e.advanceFades(1);
        double value = 0;
        check(e.animParamValue(id, "opacity", &value) && near(value, .6), "filters follow key interpolation");
        e.advanceFades(1);
        check(e.animParamValue(id, "opacity", &value) && near(value, .7), "filter ordering and clamp");
        const QString prefix = animationDriverPrefix("opacity");
        check(e.setParameter(id, prefix + "filters/1/a", .5), "animation filter is a parameter");
        e.advanceFades(.1);
        check(e.animParamValue(id, "opacity", &value) && near(value, .205), "filter setting changes reach runtime");
        ParamInfo info;
        check(e.parameterInfo(id, prefix + "wave/amplitude", &info) && info.animatable,
              "amplitude can itself be animated");
        a.tracks.front().oscillator = true;
        a.tracks.front().wave = AnimWave::Square;
        a.tracks.front().period = 4;
        a.tracks.front().center = .5;
        a.tracks.front().amplitude = .4;
        a.tracks.front().dataFilters = {};
        e.setLayerAnim(id, "opacity", &a);
        auto amplitude = e.makeLayerAnim(id, prefix + "wave/amplitude", -1);
        amplitude.tracks.front().keys = {{0, .1, 0}, {4, .1, 0}};
        e.setLayerAnim(id, prefix + "wave/amplitude", &amplitude);
        e.controlLayerAnim(id, "opacity", AnimAction::Rewind);
        e.advanceFades(.1);
        check(e.animParamValue(id, "opacity", &value) && near(std::abs(value - .5), .1),
              "nested amplitude runs before its wave");

        const int smoothIndex = e.addLayer("Smoothed");
        const auto smoothId = e.layerId(smoothIndex);
        check(e.setLayerText(smoothIndex), "smoothing fixture");
        auto smooth = e.makeLayerAnim(smoothId, "opacity", -1);
        smooth.tracks.front().keys = {{0, .1, kAnimHold}, {.1, .9, 0}, {4, .9, 0}};
        auto average = InteractionStage::made(InteractionFilter::Average);
        average.a = .5;
        smooth.tracks.front().dataFilters = Interaction::filtersToJson({average});
        Interaction config;
        config.filters = {average};
        InteractionProcessor reference;
        reference.input(config, .1);
        reference.step(config, 0);
        e.setLayerAnim(smoothId, "opacity", &smooth);
        for (int tick = 1; tick <= 4; ++tick) {
            const double raw = smooth.tracks.front().valueAt(tick * .1, tick * .1);
            reference.input(config, raw);
            const double expected = reference.step(config, .1).value();
            e.advanceFades(.1);
            check(e.animParamValue(smoothId, "opacity", &value) && near(value, expected),
                  "animation smoothing retains the existing processor history across frames");
        }
        e.controlLayerAnim(smoothId, "opacity", AnimAction::Rewind);
        check(e.animParamValue(smoothId, "opacity", &value) && near(value, .1), "rewind resets filter history");

        Interaction driver = Interaction::made(InteractionSource::Osc);
        driver.takeover = Takeover::Jump;
        driver.id = 12;
        driver.name = "Amplitude input";
        driver.oscAddress = "/amplitude";
        driver.layer = id;
        driver.param = prefix + "wave/amplitude";
        driver.filters = {math};
        e.setInteractions({driver});
        const auto parameters = e.parameters(0);
        check(std::any_of(parameters.begin(), parameters.end(),
                          [](const auto &p) { return p.path == "interaction/12/filters/1/a" && p.animatable; }),
              "interaction filter available in target picker");
        check(e.setParameter(0, "interaction/12/filters/1/a", 3), "interaction filter writable");

        InteractionEvent event;
        event.source = InteractionSource::Osc;
        event.address = "/amplitude";
        event.value = .1;
        e.feedInteraction(event);
        e.renderFrame();
        check(e.animParamValue(id, prefix + "wave/amplitude", &value) && near(value, .3),
              "OSC interaction drives animation amplitude");
        Interaction meta = Interaction::made(InteractionSource::Osc);
        meta.takeover = Takeover::Jump;
        meta.id = 13;
        meta.name = "Filter control";
        meta.oscAddress = "/filter";
        meta.layer = 0;
        meta.param = "interaction/12/filters/1/a";
        meta.filters.clear();
        e.setInteractions({e.interactions().front(), meta});
        event.address = "/filter";
        event.value = .5;
        e.feedInteraction(event);
        e.renderFrame();
        check(e.animParamValue(0, "interaction/12/filters/1/a", &value) && near(value, .5),
              "one interaction drives another interaction filter");

        QUndoStack undo;
        PresetEditor presets(&e, &undo);
        InteractionPanel interactions(&e, &undo);
        ParamAnimEditor editor(&e, &undo, id, "opacity", AnimEditor::Layout::Stacked);
        editor.resize(640, 700);
        editor.show();
        app.processEvents();
        auto *amp = editor.findChild<QWidget *>();
        for (auto *w : editor.findChildren<QWidget *>())
            if (w->property("driverPath").toString() == prefix + "wave/amplitude") amp = w;
        check(amp && amp->property("driverPath").toString() == prefix + "wave/amplitude",
              "amplitude has context driver binding");
        QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(2, 2), amp->mapToGlobal(QPoint(2, 2)));
        QApplication::sendEvent(amp, &context);
        bool gotoAnimation = false, gotoInteraction = false;
        for (auto *menu : amp->findChildren<QMenu *>())
            for (auto *action : menu->actions()) {
                gotoAnimation |= action->text().startsWith("Go to animation driver");
                gotoInteraction |= action->text().startsWith("Go to interaction driver");
            }
        check(gotoAnimation && gotoInteraction, "context menu finds both drivers");
        for (auto *menu : amp->findChildren<QMenu *>())
            menu->close();
        auto *add = editor.findChild<QPushButton *>("addAnimationDataFilter");
        check(add, "filter editor directly follows animation lane");
        add->click();
        QMenu *filterMenu = add->findChild<QMenu *>();
        check(filterMenu && filterMenu->actions().size() == kInteractionFilterCount, "all interaction filters offered");
        for (auto *action : filterMenu->actions())
            if (action->text() == interactionFilterName(InteractionFilter::Math)) {
                action->trigger();
                break;
            }
        Animation stored;
        check(e.layerAnim(id, "opacity", &stored) && stored.tracks.front().dataFilters.size() == 1,
              "adding filter changes engine");
        undo.undo();
        check(e.layerAnim(id, "opacity", &stored) && stored.tracks.front().dataFilters.isEmpty(),
              "filter addition undoes");
        undo.redo();
        check(e.layerAnim(id, "opacity", &stored) && stored.tracks.front().dataFilters.size() == 1,
              "filter addition redoes");
        QTemporaryDir dir;
        check(e.saveProject(dir.filePath("drivers.nyxmap"), {}, &error), "save filtered animation");
        check(e.loadProject(dir.filePath("drivers.nyxmap"), nullptr, &error), "reload filtered animation");
        check(e.layerAnim(id, "opacity", &stored) && stored.tracks.front().dataFilters.size() == 1,
              "filter definitions survive project round trip");
        std::cout << "animation drivers passed\n";
    } catch (const std::exception &x) {
        std::cerr << x.what() << '\n';
        return 1;
    }
    return 0;
}
