// The event lane of a timeline track: shown instead of the curve and the color lanes, a click adds a flag (one undo
// step), Delete removes the selected one; a flag that sets a switch shows its value.
#include "AnimEditor.h"
#include "EngineInternal.h"
#include "TimelinePanel.h"
#include "Widgets.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QUndoStack>
#include <cstdio>

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    theme::applyToApplication(app);
    app.setOrganizationName("NyxMapTests");
    app.setApplicationName("TimelineEventsUi");
    int failures = 0;
    auto check = [&](bool ok, const char *why) { if (!ok) { ++failures; std::printf("FAIL timeline_events_ui: %s\n", why); } };
    Engine engine;
    engine.setFadesManual(true);
    const int layer = engine.addLayer("Media");
    engine.layer(layer)->type = SourceType::Text; // state/UI tests do not allocate GPU sources
    const quint64 id = engine.layerId(layer);

    AnimTrack events;
    events.layer = id;
    events.param = "rewind";
    events.event = true;
    events.keys = {AnimKey{1, 0, 0}};
    Animation timeline;
    timeline.name = "Events";
    timeline.duration = 4;
    timeline.tracks = {events};
    const quint64 aid = engine.animation(engine.addAnimation(timeline)).id;

    QUndoStack undo;
    TimelineEditor editor(&engine, &undo);
    editor.resize(1100, 300); editor.setTimeline(aid); editor.show(); app.processEvents();
    EventLane *lane = nullptr;
    for (auto *candidate : editor.findChildren<EventLane *>()) if (candidate->isVisible()) lane = candidate;
    check(lane != nullptr, "an event track shows its event lane");
    bool otherLane = false;
    for (auto *w : editor.findChildren<CurveLane *>()) otherLane = otherLane || w->isVisible();
    for (auto *w : editor.findChildren<ColorLane *>()) otherLane = otherLane || w->isVisible();
    check(!otherLane, "no curve or color lane for an event track");
    auto keys = [&] { return engine.animation(engine.indexOfAnimation(aid)).tracks.front().keys; };
    if (lane) {
        auto click = [&](QPointF pos) {
            QMouseEvent press(QEvent::MouseButtonPress, pos, lane->mapToGlobal(pos.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(lane, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, pos, lane->mapToGlobal(pos.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(lane, &release);
        };
        // The whole duration is shown: three quarters of the way is 3 s
        click(QPointF(CurveLane::kMargin + (lane->width() - 2 * CurveLane::kMargin) * .75, lane->height() / 2.0));
        app.processEvents();
        auto k = keys();
        check(k.size() == 2 && std::abs(k[1].t - 3) < .05, "a click adds a flag where it is");
        check(engine.animation(engine.indexOfAnimation(aid)).tracks.front().event, "the track stays an event track");
        undo.undo();
        app.processEvents();
        check(keys().size() == 1, "adding a flag is one undo step");
        undo.redo();
        app.processEvents();
        for (auto *candidate : editor.findChildren<EventLane *>()) if (candidate->isVisible()) lane = candidate;
        click(QPointF(CurveLane::kMargin + (lane->width() - 2 * CurveLane::kMargin) * .25, lane->height() / 2.0)); // selects the flag at 1 s
        QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(lane, &del);
        app.processEvents();
        k = keys();
        check(k.size() == 1 && std::abs(k[0].t - 3) < .05, "Delete removes the selected flag");
    }
    // A flag that sets a switch (Layer › Visible): it shows its value, a new one sets the other state
    AnimTrack visible;
    visible.layer = id;
    visible.param = "enable";
    visible.event = true;
    visible.keys = {AnimKey{1, 0, 0}};
    Animation sets;
    sets.name = "Sets";
    sets.duration = 4;
    sets.tracks = {visible};
    const quint64 sid = engine.animation(engine.addAnimation(sets)).id;
    editor.setTimeline(sid); app.processEvents();
    EventLane *valued = nullptr;
    for (auto *candidate : editor.findChildren<EventLane *>()) if (candidate->isVisible()) valued = candidate;
    check(valued && valued->valueText(0) == "Off", "a flag shows the value it sets");
    if (valued) {
        const QPointF pos(CurveLane::kMargin + (valued->width() - 2 * CurveLane::kMargin) * .75, valued->height() / 2.0);
        QMouseEvent press(QEvent::MouseButtonPress, pos, valued->mapToGlobal(pos.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(valued, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, pos, valued->mapToGlobal(pos.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(valued, &release);
        app.processEvents();
        const auto k = engine.animation(engine.indexOfAnimation(sid)).tracks.front().keys;
        check(k.size() == 2 && k[1].v == 1, "a new flag on a switch sets the other state");
    }
    std::printf("timeline_events_ui: %d failures\n", failures);
    return failures ? 1 : 0;
}
