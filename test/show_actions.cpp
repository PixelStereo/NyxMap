// The show's actions and states as parameters (ShowParams.cpp), a layer's Visible / Solo / Seek To, the flags of an
// event track that set a value, and a choice followed by an Internal interaction: what a key, a MIDI note, another
// value or a timeline can reach without OSC.
// Standalone CTest area: nyxmap_tests --area show_actions.
#include "EngineInternal.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
// The actions run on the engine's thread once the writer is done: let them
void settle(int ms = 30)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

bool has(const std::vector<ParamInfo> &list, const QString &path)
{
    return std::any_of(list.begin(), list.end(), [&](const ParamInfo &p) { return p.path == path; });
}
} // namespace

int runShowActionTests(Engine &e, const QString &root, const QString &tmp)
{
    int failures = 0;
    auto check = [&](bool ok, const char *what) {
        if (ok) return;
        std::printf("FAIL show_actions: %s\n", what);
        ++failures;
    };
    e.newProject();
    e.setFadesManual(true);
    QString err;

    // What they act on
    Engine::Snapshot shot;
    shot.name = QStringLiteral("Intro");
    const quint64 shotId = e.snapshot(e.addSnapshot(shot)).id;
    Engine::Preset preset;
    preset.name = QStringLiteral("Look");
    const quint64 presetId = e.preset(e.addPreset(preset)).id;
    Engine::Animation timeline;
    timeline.name = QStringLiteral("Lights");
    timeline.duration = 2;
    timeline.loop = Engine::AnimLoop::Once;
    timeline.state = Engine::AnimState::Stopped;
    const quint64 timelineId = e.animation(e.addAnimation(timeline)).id;

    const QString recall = QStringLiteral("snapshot/%1/recall").arg(shotId);
    const QString play = QStringLiteral("timeline/%1/play").arg(timelineId);
    const auto comp = e.parameters(0);
    check(has(comp, "show/go") && has(comp, "show/back") && has(comp, "show/stop_all") && has(comp, "show/blackout") &&
              has(comp, "show/pause") && has(comp, recall) && has(comp, QStringLiteral("preset/%1/recall").arg(presetId)) &&
              has(comp, play) && has(comp, QStringLiteral("timeline/%1/playing").arg(timelineId)),
          "the composition lists the show's actions and states");
    const auto triggers = e.triggerParams(0), settable = e.settableParams(0);
    check(has(triggers, "show/go") && has(triggers, recall) && has(triggers, play), "an event track fires the show's actions");
    check(has(settable, "show/blackout") && has(settable, "show/pause"), "an event track sets the blackout and the pause");
    ParamInfo info;
    check(e.parameterInfo(0, recall, &info) && info.label == QStringLiteral("Snapshots › Intro"), "a snapshot's recall named after it");

    // The blackout and the pause
    check(e.setParameter(0, "show/blackout", true), "write the blackout");
    settle();
    check(e.blackout(), "the blackout goes on");
    check(e.setParameter(0, "show/blackout", false), "write the blackout off");
    settle();
    check(!e.blackout(), "the blackout goes off");
    check(e.setParameter(0, "show/pause", true), "write the pause");
    settle();
    check(e.paused(), "the show pauses");
    e.setParameter(0, "show/pause", false);
    settle();
    check(!e.paused(), "the show goes on");

    // A snapshot recalled, a timeline played
    // Recalls counted in the engine's journal (an empty snapshot is recalled, though it changes nothing)
    e.takeFrameTimingWindow();
    auto recalls = [&] {
        int n = 0;
        for (const auto &j : e.takeFrameTimingWindow().journal) n += j.kind == QLatin1String("snapshot_recall");
        return n;
    };
    int recalled = 0;
    check(e.setParameter(0, recall, true), "fire a snapshot's recall");
    settle();
    recalled = recalls();
    check(recalled == 1, "the snapshot is recalled once");
    check(e.setParameter(0, play, true), "fire a timeline's play");
    settle();
    check(e.animation(e.indexOfAnimation(timelineId)).state == Engine::AnimState::Playing, "the timeline plays");
    check(e.setParameter(0, QStringLiteral("timeline/%1/stop").arg(timelineId), true), "fire a timeline's stop");
    settle();
    check(e.animation(e.indexOfAnimation(timelineId)).state == Engine::AnimState::Stopped, "the timeline stops");

    // Its pass and its position, for an Internal interaction to follow: 0 stopped, then 1, 2… and 0..1 within it
    const QString pass = QStringLiteral("timeline/%1/pass").arg(timelineId);
    const QString where = QStringLiteral("timeline/%1/position").arg(timelineId);
    check(has(e.parameters(0), pass) && has(e.parameters(0), where), "a timeline's pass and position listed");
    double passValue = -1, whereValue = -1;
    check(e.animParamValue(0, pass, &passValue) && passValue == 0, "a stopped timeline: pass 0");
    {
        Engine::Animation looped = e.animation(e.indexOfAnimation(timelineId));
        looped.loop = Engine::AnimLoop::Loop;
        looped.repeat = 3;
        e.setAnimation(e.indexOfAnimation(timelineId), looped);
    }
    e.setParameter(0, play, true);
    settle();
    for (int i = 0; i < 250; ++i) e.advanceFades(0.01); // 2.5 s of 2 s passes
    check(e.animParamValue(0, pass, &passValue) && passValue == 2, "in its second pass: pass 2");
    check(e.animParamValue(0, where, &whereValue) && std::abs(whereValue - 0.25) < 0.02, "a quarter into it: position 0.25");
    {   // a pass shorter than the engine's shortest: its position still within 0..1
        Engine::Animation instant = e.animation(e.indexOfAnimation(timelineId));
        instant.duration = 0;
        e.setAnimation(e.indexOfAnimation(timelineId), instant);
        e.advanceFades(0.01);
        check(e.animParamValue(0, where, &whereValue) && whereValue >= 0 && whereValue <= 1, "a zero duration: position in 0..1");
        instant.duration = 2;
        e.setAnimation(e.indexOfAnimation(timelineId), instant);
    }
    for (int i = 0; i < 500; ++i) e.advanceFades(0.01); // past its 3 passes: stopped
    check(e.animParamValue(0, pass, &passValue) && passValue == 0, "its passes done, stopped: pass 0");
    e.setParameter(0, QStringLiteral("timeline/%1/stop").arg(timelineId), true);
    settle();

    // GO: the selected sequence's next step recalls its snapshot
    Sequencer::Sequence cues;
    cues.name = QStringLiteral("Main");
    Sequencer::Step step;
    step.snapshot = shotId;
    cues.steps = {step};
    e.sequencer().setCurrent(e.sequencer().add(cues));
    const quint64 sequenceId = e.sequencer().sequenceId(e.sequencer().current());
    check(has(e.parameters(0), QStringLiteral("sequence/%1/go").arg(sequenceId)), "a sequence's own GO, once it exists");
    recalls();
    recalled = 0;
    check(e.setParameter(0, "show/go", true), "fire GO");
    for (int i = 0; i < 50 && !recalled; ++i) {
        settle(10);
        e.sequencer().advance(0.02);
        recalled += recalls();
    }
    check(recalled == 1 && e.sequencer().position() == 0, "GO plays the first step");

    // Renamed: the list follows
    Engine::Snapshot renamed = e.snapshot(e.indexOfSnapshot(shotId));
    renamed.name = QStringLiteral("Opening");
    e.setSnapshot(e.indexOfSnapshot(shotId), renamed);
    check(e.parameterInfo(0, recall, &info) && info.label == QStringLiteral("Snapshots › Opening"), "renamed, its recall too");

    // A layer: Visible, Solo (never saved with it), and a video's Seek To
    const int vi = e.addLayer(QStringLiteral("Video"));
    if (!e.setLayerVideo(vi, root + "/media/h264.mp4", &err)) {
        check(false, "open test video (sh test/make_media.sh)");
        e.setFadesManual(false);
        return failures;
    }
    const quint64 video = e.layerId(vi);
    check(e.setParameter(video, "play", false), "pause the video");
    auto layerOf = [&](quint64 id) { return e.layer(e.indexOfId(id)); };
    check(e.setParameter(video, "enable", false) && !layerOf(video)->enabled, "Visible switches the layer off");
    check(e.parameterInfo(video, "enable", &info) && !info.stored && e.parameterInfo(video, "opacity", &info) && info.stored &&
              e.parameterInfo(0, "show/blackout", &info) && !info.stored,
          "Visible, Solo, the blackout: not offered to presets (not stored)");
    check(e.setParameter(video, "solo", true) && layerOf(video)->solo, "Solo");
    {
        const QJsonObject params = e.layerJson(e.indexOfId(video)).value(QStringLiteral("params")).toObject();
        check(!params.contains("enable") && !params.contains("solo") && !params.contains("seek"), "Visible, Solo, Seek To not saved");
    }
    e.setParameter(video, "enable", true);
    e.setParameter(video, "solo", false);
    auto position = [&] {
        Engine::Lock lk(&e.mutex());
        return layerOf(video)->position();
    };
    check(e.setParameter(video, "seek", 2.5) && std::abs(position() - 2.5) < 0.05, "Seek To moves the playhead");

    // Flags that set values: Play off, a blend mode, Seek To, the blackout (the composition's)
    check(e.parameterInfo(video, "blend_mode", &info) && info.choices.size() > 1, "blend modes");
    const QStringList blends = info.choices;
    auto track = [](quint64 layer, const char *param, double at, double v) {
        Engine::AnimTrack t;
        t.layer = layer;
        t.param = QString::fromLatin1(param);
        t.event = true;
        t.keys = {Engine::AnimKey{at, v, 0}};
        return t;
    };
    e.setParameter(video, "play", true);
    Engine::Animation sets;
    sets.name = QStringLiteral("Sets");
    sets.duration = 1;
    sets.loop = Engine::AnimLoop::Once;
    sets.state = Engine::AnimState::Stopped;
    sets.tracks = {track(video, "play", 0.25, 0), track(video, "blend_mode", 0.25, 1), track(video, "seek", 0.5, 1.5),
                   track(0, "show/blackout", 0.5, 1)};
    const quint64 setsId = e.animation(e.addAnimation(sets)).id;
    e.controlAnimation(setsId, Engine::AnimAction::Play);
    for (int frame = 0; frame < 45; ++frame) e.advanceFades(1.0 / 60);
    settle();
    check(!e.parameterValue(video, "play").toBool(), "a flag switches Play off");
    check(e.parameterValue(video, "blend_mode").toString() == blends.value(1), "a flag sets a blend mode");
    check(std::abs(position() - 1.5) < 0.05, "a flag seeks the video");
    check(e.blackout(), "a flag sets the show's blackout");
    e.setBlackout(false, 0);

    // Saved and opened again with their values
    const QString file = tmp + QStringLiteral("/show-actions.nyxmap");
    check(e.saveProject(file, {}, &err), "save the project");
    e.newProject();
    check(e.loadProject(file, nullptr, &err), "open the project again");
    bool kept = false;
    for (int i = 0; i < e.animationCount(); ++i)
        for (const auto &t : e.animation(i).tracks)
            if (t.event && t.param == QLatin1String("seek") && t.keys.size() == 1 && std::abs(t.keys[0].v - 1.5) < 1e-9)
                kept = true;
    check(kept, "a flag's value is saved");

    // A choice followed by an Internal interaction: as its index
    const quint64 v2 = e.layerId(e.indexOfId(video));
    const int fi = e.addLayer(QStringLiteral("Follower"));
    e.layer(fi)->type = SourceType::Text;
    const quint64 follower = e.layerId(fi);
    Interaction link2 = Interaction::made(InteractionSource::Internal);
    link2.sourceLayer = v2;
    link2.sourceParam = QStringLiteral("blend_mode");
    link2.filters = {InteractionStage::remap(0, blends.size() - 1, 0, 1)}; // a choice comes as its index
    link2.layer = follower;
    link2.param = QStringLiteral("opacity");
    link2.takeover = Takeover::Jump;
    e.addInteraction(link2);
    check(e.setParameter(v2, "blend_mode", blends.last()), "set the last blend mode");
    e.renderFrame();
    check(std::abs(e.parameterValue(follower, "opacity").toDouble() - 1.0) < 1e-6, "a choice followed as its index");

    e.setFadesManual(false);
    if (!failures) std::printf("show_actions: OK\n");
    return failures;
}
