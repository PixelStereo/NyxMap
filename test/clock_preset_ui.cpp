#include "Engine.h"
#include "LayerInspector.h"
#include "PresetEditor.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QUndoStack>
#include <cmath>
#include <cstdio>

int runClockPresetUiTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *reason) {
        if (!ok) { ++failures; std::printf("FAIL clock_preset_ui: %s\n", reason); }
    };
    auto pump = [] {
        for (int k = 0; k < 3; ++k) {
            qApp->processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    };
    engine.setFadesManual(true);
    const int row = engine.addLayer("Clock preset controls");
    const quint64 id = engine.layerId(row);
    QUndoStack undo;
    PresetEditor presets(&engine, &undo);
    Engine::Preset empty; empty.name = "Individual clock settings"; empty.fade = 0;
    const int pi = engine.addPreset(empty);
    presets.select(engine.preset(pi).id);
    presets.setEditing(true);
    LayerInspector inspector(&engine, &undo);
    inspector.resize(700, 1600); inspector.setLayer(row); inspector.show();
    auto showSource = [&] {
        inspector.rebuild();
        for (auto *tabs : inspector.findChildren<QTabWidget *>("layerCategoryTabs"))
            for (int k = 0; k < tabs->count(); ++k)
                if (tabs->tabText(k) == "Source") tabs->setCurrentIndex(k);
        pump();
    };
    auto fieldFor = [&](const QString &path, bool label) -> QWidget * {
        for (auto *w : inspector.findChildren<QWidget *>())
            if (w->isVisible() && bool(qobject_cast<QLabel *>(w)) == label &&
                w->property("valuePaths").toStringList() == QStringList{path}) return w;
        return nullptr;
    };
    auto click = [](QWidget *w, Qt::KeyboardModifiers modifiers) {
        const QPointF pos(3, 3), global(w->mapToGlobal(QPoint(3, 3)));
        QMouseEvent down(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, modifiers);
        QMouseEvent up(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::NoButton, modifiers);
        QApplication::sendEvent(w, &down); QApplication::sendEvent(w, &up);
    };
    auto exercise = [&](const QString &path) {
        QWidget *field = fieldFor(path, false), *label = fieldFor(path, true);
        check(field != nullptr, qPrintable(path + " has a directly selectable field"));
        if (!field) return;
        const auto before = engine.parameterValue(id, path);
        const auto count = engine.preset(pi).values.size();
        click(field, Qt::AltModifier);
        check(presets.inSelected(id, {path}) && engine.preset(pi).values.size() == count + 1,
              qPrintable(path + ": Alt-click stores exactly the individual setting"));
        check(engine.parameterValue(id, path) == before, qPrintable(path + ": preset selection leaves the live value unchanged"));
        undo.undo(); check(!presets.inSelected(id, {path}), qPrintable(path + ": Undo removes the captured setting"));
        undo.redo(); check(presets.inSelected(id, {path}), qPrintable(path + ": Redo restores the setting"));
        click(label ? label : field, label ? Qt::NoModifier : Qt::AltModifier);
        check(!presets.inSelected(id, {path}), qPrintable(path + ": click removes the setting from the selected preset"));
        QContextMenuEvent event(QContextMenuEvent::Mouse, {3, 3}, field->mapToGlobal(QPoint(3, 3)));
        QApplication::sendEvent(field, &event);
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        bool hasPreset = false;
        if (menu) for (auto *a : menu->actions()) hasPreset |= a->text() == "Preset";
        check(hasPreset, qPrintable(path + ": the field exposes the standard Preset context menu"));
        if (menu) menu->close();
        pump();
    };
    check(clockKindName(ClockKind::Stopwatch) == "Chronometer", "the public generator name is Chronometer");
    check(clockGeneratorOfPath("/NyxMap/Generators/Chronometer") == ClockKind::Stopwatch, "the Chronometer generator's path");
    for (auto kind : {ClockKind::Stopwatch, ClockKind::Countdown, ClockKind::Clock}) {
        check(engine.setLayerClock(row, kind), "create the requested generator");
        engine.setParameter(id, "clock/format", "custom");
        engine.setParameter(id, "clock/custom_format", kind == ClockKind::Clock ? "HH:mm:ss" : "mm:ss:ff");
        if (kind == ClockKind::Countdown) engine.setParameter(id, "clock/at_zero", "text");
        showSource();
        for (const auto &path : QStringList{"clock/format", "clock/custom_format"}) exercise(path);
        if (kind == ClockKind::Stopwatch) exercise("clock/offset");
        if (kind != ClockKind::Clock) {
            exercise("clock/fps"); exercise("clock/play"); exercise("clock/elapsed");
        } else exercise("clock/zone");
        if (kind == ClockKind::Countdown) {
            for (const auto &path : QStringList{"clock/target", "clock/duration", "clock/at_zero", "clock/zero_text"}) exercise(path);
            engine.setParameter(id, "clock/target", "time_of_day"); showSource();
            exercise("clock/target_time"); exercise("clock/zone");
            engine.setParameter(id, "clock/at_zero", "overtime"); showSource();
            auto *color = inspector.findChild<QWidget *>();
            for (auto *w : inspector.findChildren<QWidget *>())
                if (!qobject_cast<QLabel *>(w) && w->isVisible() &&
                    w->property("valuePaths").toStringList().contains("clock/overtime_color/r")) { color = w; break; }
            check(color && color->property("valuePaths").toStringList().size() == 4, "overtime color exposes RGBA preset capture");
        }
    }
    // The two checkboxes are enabled only for a built-in Clock format.
    engine.setLayerClock(row, ClockKind::Clock); engine.setParameter(id, "clock/format", "24h"); showSource();
    exercise("clock/seconds"); exercise("clock/date");

    TimecodeSettings settings; settings.internal = TimecodeSettings::Internal::FreeRun;
    check(engine.setLayerTimecode(row, settings), "create a free-run Timecode");
    engine.setParameter(id, "tc/play", false); showSource();
    for (const auto &path : QStringList{"tc/source", "tc/internal", "tc/rate", "tc/offset", "tc/output",
                                       "tc/play", "tc/position", "tc/in", "tc/out", "tc/play_mode"}) exercise(path);
    engine.setParameter(id, "tc/play", false); engine.setParameter(id, "tc/position", 42.0);
    presets.addToSelected(id, {"tc/rate"});
    engine.setParameter(id, "tc/rate", "30"); engine.setParameter(id, "tc/offset", 125);
    engine.recallPreset(pi);
    check(engine.layerTimecode(row)->rate == TcRate::Fps25 && engine.layerTimecode(row)->offset == 125 &&
          std::abs(engine.layerTimecodeClock(row)->position() - 42) < 1e-6,
          "a rate-only preset preserves the offset and free-run transport");
    Engine::Snapshot snapshot; snapshot.name = "Timecode source"; snapshot.fade = 0; snapshot.layers = engine.captureLayers(true);
    const int si = engine.addSnapshot(snapshot);
    engine.setLayerText(row); engine.recallSnapshot(si);
    check(engine.layerTimecode(row).has_value(), "a snapshot restores Timecode after replacing it with plain text");
    engine.setParameter(id, "tc/rate", "30"); engine.recallSnapshot(si);
    check(engine.layerTimecode(row)->rate == TcRate::Fps25, "a snapshot restores changed Timecode configuration");
    QTemporaryDir files; QString error;
    check(engine.saveProject(files.filePath("clocks.nyxmap"), {}, &error) &&
          engine.loadProject(files.filePath("clocks.nyxmap"), nullptr, &error) && engine.preset(pi).indexOf(id, "tc/rate") >= 0,
          "individual Timecode settings survive saving and reopening the project");
    return failures;
}
