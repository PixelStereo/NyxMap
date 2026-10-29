// Event tracks of a timeline: each flag fires its action (here a video's Source › Rewind) once as the playhead
// reaches it — at the start, within a pass, at a pass's end, across loops and ping-pong turns — but not when sought.
// Standalone CTest area: nyxmap_tests --area timeline_events.
#include "Engine.h"

#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
constexpr double kAway = 3.0; // where the paused video is put back after each rewind

struct VideoUnderTest {
    Engine &e;
    int index = -1;
    quint64 layer = 0;
    double position() const
    {
        Engine::Lock lk(&e.mutex());
        return e.layer(index)->position();
    }
};

// Plays `timeline` for `seconds` and counts the events it fired (the engine's journal), checking that the video was
// rewound in some frame each time one did
int countRewinds(VideoUnderTest &f, Engine::Animation timeline, double seconds, bool seekMidway = false)
{
    timeline.state = Engine::AnimState::Stopped;
    f.e.seekLayer(f.index, kAway);
    const int at = f.e.addAnimation(timeline);
    const quint64 id = f.e.animation(at).id;
    const double dt = 1.0 / 60;
    int fired = 0, rewound = 0;
    f.e.takeFrameTimingWindow(); // the journal from here
    auto taken = [&] {
        int now = 0;
        for (const auto &j : f.e.takeFrameTimingWindow().journal)
            if (j.kind == QLatin1String("timeline") && j.detail == QStringLiteral("%1 event rewind").arg(id)) ++now;
        fired += now;
        if (f.position() >= 0.5) return;
        rewound += now > 0;
        f.e.seekLayer(f.index, kAway);
    };
    f.e.controlAnimation(id, Engine::AnimAction::Play);
    taken();
    for (int frame = 1; frame <= int(std::round(seconds / dt)); ++frame) {
        if (seekMidway && frame == int(std::round(seconds / dt / 2))) {
            f.e.controlAnimation(id, Engine::AnimAction::Seek, 0.1); // backwards over the flag: nothing fires
            f.e.controlAnimation(id, Engine::AnimAction::Play);
        }
        f.e.advanceFades(dt);
        taken();
    }
    f.e.removeAnimation(f.e.indexOfAnimation(id));
    if (fired && !rewound) return -1; // fired, but the video did not go back
    return fired;
}

Engine::Animation rewindAt(quint64 layer, std::vector<double> times, Engine::AnimLoop loop, int repeat)
{
    Engine::AnimTrack t;
    t.layer = layer;
    t.param = QStringLiteral("rewind");
    t.event = true;
    for (double time : times) t.keys.push_back(Engine::AnimKey{time, 0, 0});
    Engine::Animation a;
    a.name = QStringLiteral("Events");
    a.duration = 1.0;
    a.loop = loop;
    a.repeat = repeat;
    a.tracks = {t};
    return a;
}
} // namespace

int runTimelineEventTests(Engine &e, const QString &root, const QString &tmp)
{
    int failures = 0;
    auto check = [&](bool ok, const char *what, int got = -1) {
        if (ok) return;
        std::printf("FAIL timeline_events: %s (got %d)\n", what, got);
        ++failures;
    };
    e.newProject();
    e.setFadesManual(true);
    VideoUnderTest f{e};
    f.index = e.addLayer(QStringLiteral("Video"));
    QString err;
    if (!e.setLayerVideo(f.index, root + "/media/h264.mp4", &err)) {
        check(false, "open test video (sh test/make_media.sh)");
        e.setFadesManual(false);
        return failures;
    }
    f.layer = e.layerId(f.index);
    check(e.setParameter(f.layer, QStringLiteral("play"), false), "pause the video");

    // The action is listed among the ones an event track can fire, and fires on its own (OSC, interactions)
    const auto triggers = e.triggerParams(f.layer);
    check(std::any_of(triggers.begin(), triggers.end(), [](const ParamInfo &p) { return p.path == QLatin1String("rewind"); }),
          "Source › Rewind is a trigger of a video layer");
    e.seekLayer(f.index, kAway);
    check(e.setParameter(f.layer, QStringLiteral("rewind"), true) && f.position() < 0.5, "rewind on its own");

    int n = 0;
    n = countRewinds(f, rewindAt(f.layer, {0.5}, Engine::AnimLoop::Once, 0), 2.0);
    check(n == 1, "once: a flag in the pass fires once", n);
    n = countRewinds(f, rewindAt(f.layer, {0.0}, Engine::AnimLoop::Once, 0), 2.0);
    check(n == 1, "once: a flag at the start fires as it plays", n);
    n = countRewinds(f, rewindAt(f.layer, {1.0}, Engine::AnimLoop::Once, 0), 2.0);
    check(n == 1, "once: a flag at the end fires as it ends", n);
    n = countRewinds(f, rewindAt(f.layer, {0.5}, Engine::AnimLoop::Loop, 3), 4.0);
    check(n == 3, "loop x3: once per pass", n);
    n = countRewinds(f, rewindAt(f.layer, {0.0}, Engine::AnimLoop::Loop, 3), 4.0);
    check(n == 3, "loop x3: a flag at the start, no extra pass after the last", n);
    n = countRewinds(f, rewindAt(f.layer, {0.25}, Engine::AnimLoop::PingPong, 2), 3.0);
    check(n == 2, "ping-pong x2: there and back", n);
    n = countRewinds(f, rewindAt(f.layer, {1.0}, Engine::AnimLoop::PingPong, 2), 3.0);
    check(n == 1, "ping-pong x2: a flag at the turn fires once", n);
    n = countRewinds(f, rewindAt(f.layer, {0.8}, Engine::AnimLoop::Once, 0), 2.0, true);
    check(n == 2, "a seek back over a flag does not fire; played again it does", n);

    // Off, or a locked layer: nothing fires
    auto off = rewindAt(f.layer, {0.5}, Engine::AnimLoop::Once, 0);
    off.tracks.front().enabled = false;
    n = countRewinds(f, off, 2.0);
    check(n == 0, "a track turned off fires nothing", n);

    // Saved and opened again: an event track with its flags (the layer keeps its id)
    e.addAnimation(rewindAt(f.layer, {0.25, 0.75}, Engine::AnimLoop::Loop, 0));
    const QString file = tmp + QStringLiteral("/timeline-events.nyxmap");
    check(e.saveProject(file, {}, &err), "save the project");
    e.newProject();
    check(e.loadProject(file, nullptr, &err), "open the project again");
    bool found = false;
    for (int i = 0; i < e.animationCount(); ++i)
        for (const auto &t : e.animation(i).tracks)
            if (t.event && t.param == QLatin1String("rewind") && t.layer == f.layer && t.keys.size() == 2 &&
                std::abs(t.keys[0].t - 0.25) < 1e-9 && std::abs(t.keys[1].t - 0.75) < 1e-9)
                found = true;
    check(found, "an event track is saved and opened again");

    e.setFadesManual(false);
    if (!failures) std::printf("timeline_events: OK\n");
    return failures;
}
