#include "AnimEditor.h"
#include "EngineInternal.h"
#include "TimelinePanel.h"
#include "ColorPicker.h"
#include "Commands.h"
#include "Widgets.h"
#include <QApplication>
#include <QDialog>
#include <QContextMenuEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <cmath>
#include <cstdio>

static AnimKey key(double time, std::array<double, 3> rgb, int curve = 0)
{
    AnimKey k{time, 0, curve}; k.rgb = rgb; return k;
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    theme::applyToApplication(app);
    app.setOrganizationName("NyxMapTests");
    app.setApplicationName("ColorTimeline");
    int failures = 0;
    auto check = [&](bool ok, const char *why) { if (!ok) { ++failures; std::printf("FAIL color_timeline: %s\n", why); } };
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-4; };
    Engine engine;
    QString error;
    engine.setFadesManual(true);
    const int layer = engine.addLayer("RGB source");
    engine.layer(layer)->type = SourceType::Text; // state/UI tests do not allocate GPU sources
    const quint64 id = engine.layerId(layer);
    const auto colors = engine.animatableColors(id);
    check(std::any_of(colors.begin(), colors.end(), [](const auto &p) { return p.path == "text/color"; }), "text RGB is a grouped target");
    check(std::any_of(colors.begin(), colors.end(), [](const auto &p) { return p.path == "add"; }), "color adjustment RGB is a grouped target");
    check(std::none_of(colors.begin(), colors.end(), [](const auto &p) { return p.path == "position"; }), "non-color components are never grouped");
    check(engine.setParameter(id, "text/color/a", .37), "set independent alpha");
    AnimTrack track; track.layer = id; track.param = "text/color"; track.color = true;
    track.keys = {key(0, {1, 0, 0}), key(4, {0, 0, 1})};
    auto rgb = track.colorAt(1);
    check(near(rgb[0], .75) && near(rgb[1], 0) && near(rgb[2], .25), "RGB interpolation shares one normalized time");
    track.keys[0].curve = 1;
    rgb = track.colorAt(1);
    check(rgb[0] > .75 && rgb[2] < .25, "easing applies to all three components");
    track.keys[0].curve = kAnimHold;
    check(track.colorAt(3.99) == track.keys[0].rgb && track.colorAt(4) == track.keys[1].rgb, "hold changes color exactly at the next key");
    track.keys[0].curve = 0;
    check(track.colorAt(-1) == track.keys.front().rgb && track.colorAt(8) == track.keys.back().rgb, "end colors extend outside the key interval");
    check(std::isnan(AnimTrack().colorAt(0)[0]), "empty color track has no value");
    Animation animation; animation.name = "Color fade"; animation.duration = 4; animation.loop = AnimLoop::Once; animation.tracks = {track};
    AnimTrack scalar; scalar.layer = id; scalar.param = "opacity"; scalar.keys = {{0, .2, 0}, {4, 1, 0}};
    animation.tracks.push_back(scalar);
    const int ai = engine.addAnimation(animation);
    const quint64 aid = engine.animation(ai).id;
    engine.controlAnimation(aid, AnimAction::Play);
    engine.advanceFades(1);
    check(engine.animColorValue(id, "text/color", &rgb) && near(rgb[0], .75) && near(rgb[2], .25), "playback writes a complete RGB color");
    check(near(engine.parameterValue(id, "text/color/a").toDouble(), .37), "RGB track leaves alpha unchanged");
    check(near(engine.parameterValue(id, "opacity").toDouble(), .4), "a numeric track still plays beside RGB");
    engine.controlAnimation(aid, AnimAction::Seek, 3);
    check(engine.animColorValue(id, "text/color", &rgb) && near(rgb[0], .25) && near(rgb[2], .75), "scrubbing evaluates RGB immediately");
    engine.layer(layer)->locked = true;
    engine.controlAnimation(aid, AnimAction::Seek, 4);
    std::array<double, 3> locked;
    check(engine.animColorValue(id, "text/color", &locked) && locked == rgb, "locked layer rejects the entire RGB write");
    engine.layer(layer)->locked = false;
    auto disabled = engine.animation(ai); disabled.tracks[0].enabled = false;
    engine.setAnimation(ai, disabled);
    engine.controlAnimation(aid, AnimAction::Seek, 0);
    check(engine.animColorValue(id, "text/color", &locked) && locked == rgb, "disabled color track does not write");
    engine.setAnimation(ai, animation);
    QUndoStack undo;
    auto before = engine.animation(ai), after = before;
    after.tracks[0].keys[1].rgb = {0, 1, 0};
    undo.push(new cmd::SetAnimation(&engine, aid, before, after, "Color key"));
    check(engine.animation(ai).tracks[0].keys[1].rgb[1] == 1, "color edit reaches engine");
    undo.undo(); check(engine.animation(ai).tracks[0].keys[1].rgb[2] == 1, "one Undo restores the complete color");
    undo.redo(); check(engine.animation(ai).tracks[0].keys[1].rgb[1] == 1, "Redo restores the complete color");
    undo.undo();
    QTemporaryDir directory;
    const QString project = directory.filePath("color.nyxmap");
    check(engine.saveProject(project, {}, &error), "project saves color tracks");
    QFile file(project);
    check(file.open(QIODevice::ReadOnly), "saved project is readable");
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    const auto savedAnimation = root.value("timelines").toArray()[0].toObject();
    const auto savedTracks = savedAnimation.value("tracks").toArray();
    Animation loaded = animation; loaded.id = aid; loaded.tracks.clear();
    for (const auto &value : savedTracks)
        loaded.tracks.push_back(layerAnimFromJson(value.toObject(), id).tracks[0]);
    check(loaded.tracks.size() == 2 && loaded.tracks[0].color && !loaded.tracks[1].color &&
          loaded.tracks[0].keys[1].rgb == track.keys[1].rgb, "typed RGB keys and numeric keys round-trip through project JSON");
    Engine restored;
    const int restoredLayer = restored.addLayer("Restored color");
    restored.layer(restoredLayer)->type = SourceType::Text;
    restored.layer(restoredLayer)->id = id;
    restored.addAnimation(loaded);
    restored.controlAnimation(aid, AnimAction::Seek, 2);
    check(restored.animColorValue(id, "text/color", &rgb) && near(rgb[0], .5) && near(rgb[2], .5), "restored color animation plays");

    TimelineEditor editor(&engine, &undo);
    editor.resize(1100, 420); editor.setTimeline(aid); editor.show(); app.processEvents();
    // A widget's picture at a point in its own coordinates (a Retina screen grabs it at twice its size)
    auto colorAt = [](QWidget *w, int x, int y) {
        const QImage image = w->grab().toImage();
        const qreal ratio = image.devicePixelRatio();
        return image.pixelColor(int(x * ratio), int(y * ratio));
    };
    auto lanes = editor.findChildren<ColorLane *>();
    ColorLane *lane = nullptr;
    for (auto *candidate : lanes) if (candidate->isVisible()) lane = candidate;
    check(lane != nullptr, "timeline shows one RGB lane, with the numeric track kept separate");
    bool menuHasColor = false;
    for (auto *menu : editor.findChildren<QMenu *>()) for (auto *action : menu->actions())
        if (action->text() == "Color (RGB)") menuHasColor = true;
    check(menuHasColor, "parameter picker offers the grouped text color");
    if (lane) {
        const QColor middle = colorAt(lane, lane->width() / 2, 48);
        check(std::abs(middle.red() - 128) < 5 && middle.green() < 4 && std::abs(middle.blue() - 128) < 5,
              "gradient preview matches the engine's RGB midpoint");
        auto mouse = [&](QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, pos, lane->mapToGlobal(pos.toPoint()), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(lane, &event);
        };
        const QPointF start(lane->width() - CurveLane::kMargin, 20), moved(lane->width() / 2.0, 20);
        mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, moved, Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, moved - QPointF(8, 0), Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, moved - QPointF(8, 0), Qt::LeftButton, Qt::NoButton);
        check(engine.animation(ai).tracks[0].keys[1].t < 2.01 && engine.animation(ai).tracks[0].keys[1].rgb[2] == 1,
              "dragging a color key moves its time and preserves RGB");
        undo.undo(); editor.reload(); app.processEvents();
        check(engine.animation(ai).tracks[0].keys[1].t == 4, "one Undo restores the whole key drag");
        // Undo may rebuild the rows: acquire the current lane again.
        lane = nullptr;
        for (auto *candidate : editor.findChildren<ColorLane *>()) if (candidate->isVisible()) lane = candidate;
        if (lane) {
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                check(dialog != nullptr, "double-click opens the shared color picker");
                if (!dialog) return;
                auto *picker = dialog->findChild<ColorPicker *>();
                check(picker && !picker->hasAlpha(), "RGB key picker cannot change source alpha");
                if (picker) picker->setColor(Qt::green);
                dialog->accept();
            });
            mouse(QEvent::MouseButtonDblClick, {CurveLane::kMargin, 20}, Qt::LeftButton, Qt::LeftButton);
            check(engine.animation(ai).tracks[0].keys[0].rgb[1] == 1 && engine.animation(ai).tracks[0].keys[0].rgb[0] == 0,
                  "double-click changes RGB as one key edit");
            undo.undo(); editor.reload(); app.processEvents();
        }
    }
    // Exercise key creation/cancellation and transition preview without involving transport.
    ColorLane isolated;
    isolated.resize(500, 110); isolated.setTrack(track, 4); isolated.setView(0, 4); isolated.show();
    AnimTrack edited = track;
    int edits = 0;
    QObject::connect(&isolated, &ColorLane::edited, &isolated, [&](const AnimTrack &t, int) { edited = t; ++edits; });
    auto click = [&](QEvent::Type type, Qt::MouseButtons buttons) {
        const QPointF pos(250, 45);
        QMouseEvent event(type, pos, isolated.mapToGlobal(pos.toPoint()), Qt::LeftButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(&isolated, &event);
    };
    QTimer::singleShot(0, [&] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
    });
    click(QEvent::MouseButtonPress, Qt::LeftButton); click(QEvent::MouseButtonRelease, Qt::NoButton);
    check(edits == 0, "canceling a new color key leaves data and undo history untouched");
    QTimer::singleShot(0, [&] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            if (auto *picker = dialog->findChild<ColorPicker *>()) picker->setColor(Qt::green);
            dialog->accept();
        }
    });
    click(QEvent::MouseButtonPress, Qt::LeftButton); click(QEvent::MouseButtonRelease, Qt::NoButton);
    check(edits == 1 && edited.keys.size() == 3 && near(edited.keys[1].t, 2) && edited.keys[1].rgb[1] == 1,
          "one click creates a complete RGB key in time order");
    QKeyEvent remove(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(&isolated, &remove);
    check(edits == 2 && edited.keys.size() == 2, "Delete removes the selected color key");
    QContextMenuEvent context(QContextMenuEvent::Mouse, {10, 20}, isolated.mapToGlobal(QPoint(10, 20)));
    QApplication::sendEvent(&isolated, &context);
    bool holdAction = false;
    for (auto *menu : isolated.findChildren<QMenu *>()) for (auto *action : menu->actions())
        if (action->text() == "Hold (steps)") { action->trigger(); holdAction = true; }
    check(holdAction && edited.keys[0].curve == kAnimHold, "transition menu changes the complete RGB segment");
    const QColor held = colorAt(&isolated, 250, 48);
    check(held.red() > 250 && held.green() < 3 && held.blue() < 3, "hold preview remains at the first color");
    for (auto *menu : isolated.findChildren<QMenu *>()) menu->close();
    // A current-value color has the same playback preview and an unmistakable first-key marker.
    app.processEvents();
    isolated.setTrack(track, 4);
    isolated.setCurrentColor([] { return std::array<double, 3>{.2, .4, .6}; });
    QApplication::sendEvent(&isolated, &context);
    auto *currentMenu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    QAction *startCurrent = nullptr;
    if (currentMenu) for (auto *action : currentMenu->actions())
        if (action->text() == "Start from the current value") startCurrent = action;
    check(startCurrent && startCurrent->isCheckable(), "first color key offers Start from the current value");
    if (startCurrent) startCurrent->trigger();
    check(edited.keys[0].isCurrentValue, "current-value color setting is emitted as one undoable edit");
    if (currentMenu) currentMenu->close();
    isolated.setLive({.2, .4, .6});
    const QColor currentMiddle = colorAt(&isolated, 250, 48);
    check(std::abs(currentMiddle.red() - 26) < 5 && std::abs(currentMiddle.green() - 51) < 5 &&
          std::abs(currentMiddle.blue() - 204) < 5, "RGB gradient previews the captured start rather than the placeholder");
    int amber = 0;
    const QImage currentPreview = isolated.grab().toImage();
    const qreal ratio = currentPreview.devicePixelRatio();
    for (int y = 7; y < 34; ++y) for (int x = 0; x < 25; ++x) {
        const QColor pixel = currentPreview.pixelColor(int(x * ratio), int(y * ratio));
        if (pixel.red() > 220 && pixel.green() > 150 && pixel.green() < 225 && pixel.blue() < 130) ++amber;
    }
    check(amber > 20, "current color key has a distinct amber diamond marker");
    // Uncompiled shader placeholders exercise target cleanup/Undo without allocating GPU objects.
    Engine cleanup;
    const int ci = cleanup.addLayer("FX cleanup");
    cleanup.layer(ci)->type = SourceType::Text;
    const quint64 cid = cleanup.layerId(ci);
    cleanup.layer(ci)->effects.push_back(std::make_unique<IsfInstance>());
    cleanup.layer(ci)->effects.push_back(std::make_unique<IsfInstance>());
    const QString oldFirst = cleanup.shaderPrefix(cid, 0) + "speed";
    const QString oldSecond = cleanup.shaderPrefix(cid, 1) + "speed";
    auto one = cleanup.makeLayerAnim(cid, oldFirst, -1), two = cleanup.makeLayerAnim(cid, oldSecond, -1);
    one.tracks[0].keys = {{0, 1, 0}}; two.tracks[0].keys = {{0, 2, 0}};
    cleanup.setLayerAnim(cid, oldFirst, &one); cleanup.setLayerAnim(cid, oldSecond, &two);
    AnimTrack firstTarget; firstTarget.layer = cid; firstTarget.param = oldFirst; firstTarget.keys = {{0, 1, 0}};
    AnimTrack secondTarget = firstTarget; secondTarget.param = oldSecond; secondTarget.keys[0].v = 2;
    Animation targets; targets.tracks = {firstTarget, secondTarget};
    targets.tracks[0].keys[0].isCurrentValue = true;
    const int targetIndex = cleanup.addAnimation(targets);
    cleanup.controlAnimation(cleanup.animation(targetIndex).id, AnimAction::Seek, .75);
    const double capturedStart = cleanup.animation(targetIndex).tracks[0].captured;
    const auto beforeFx = cleanup.effectsJson(ci);
    const auto beforeTargets = cleanup.captureAnimationTargets(cid);
    cleanup.removeEffect(ci, 0);
    QUndoStack cleanupUndo;
    cleanupUndo.push(new cmd::SetEffects(&cleanup, ci, beforeFx, "Remove FX", beforeTargets));
    Animation survivor;
    check(cleanup.layerAnim(cid, oldFirst, &survivor) && survivor.tracks[0].keys[0].v == 2 &&
          !cleanup.layerAnim(cid, oldSecond, nullptr), "removed FX local animation is deleted and duplicate survivor is remapped");
    check(cleanup.animation(targetIndex).tracks.size() == 1 && cleanup.animation(targetIndex).tracks[0].keys[0].v == 2,
          "shared timeline removes only the deleted FX track");
    cleanupUndo.undo();
    check(cleanup.layerAnim(cid, oldFirst, nullptr) && cleanup.layerAnim(cid, oldSecond, nullptr) &&
          cleanup.animation(targetIndex).tracks.size() == 2, "FX Undo restores local and shared animations together");
    check(std::isfinite(capturedStart) && cleanup.animation(targetIndex).tracks[0].captured == capturedStart &&
          cleanup.animation(targetIndex).clock == .75,
          "FX Undo restores captured start values without rewinding shared playback");
    cleanupUndo.redo();
    check(cleanup.animation(targetIndex).tracks.size() == 1 && !cleanup.layerAnim(cid, oldSecond, nullptr),
          "FX Redo repeats the target cleanup");
    cleanup.setEffectsJson(ci, {});
    check(cleanup.layerAnims(cid).empty() && cleanup.animation(targetIndex).tracks.empty(),
          "removing the complete FX chain leaves no orphan animation");
    if (argc > 1) editor.grab().save(QString::fromLocal8Bit(argv[1]));
    std::printf("color_timeline: %d failures\n", failures);
    return failures ? 1 : 0;
}
