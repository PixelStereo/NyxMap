#include "Engine.h"
#include "Osc.h"
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>
#include <QCoreApplication>

int runShowControlTests(Engine &e)
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) { if (!ok) { ++failures; std::printf("FAIL show_control: %s\n", why); } };
    e.newProject(); e.setFadesManual(true);
    OscServer server(&e);
    auto get = [&](const QString &path) {
        int status = 0;
        const auto reply = QJsonDocument::fromJson(server.httpGet(path + "?VALUE", &status)).object();
        check(status == 200, "OSCQuery endpoint exists");
        return QJsonDocument::fromJson(reply.value("VALUE").toArray().at(0).toString().toUtf8()).object();
    };
    check(get("/show/catalog").value("sequences").toArray().isEmpty(), "empty show returns empty catalogs");
    const quint64 layer = e.layerId(e.addLayer("Control target"));
    e.setParameter(layer, "opacity", .2);
    Engine::Snapshot snapshot; snapshot.name = "Same name"; snapshot.layers = e.captureLayers(); snapshot.fade = 0;
    const int si = e.addSnapshot(snapshot); const quint64 sid = e.snapshot(si).id;
    Engine::Preset preset; preset.name = "Live preset"; preset.values.push_back({layer, "opacity", .7, 0, QString()});
    const int pi = e.addPreset(preset); const quint64 pid = e.preset(pi).id;
    Animation timeline; timeline.name = "Fade"; timeline.duration = 2; timeline.loop = AnimLoop::Once;
    AnimTrack track; track.layer = layer; track.param = "opacity"; track.keys = {{0, 0, 0}, {2, 1, 0}};
    timeline.tracks = {track}; const int ti = e.addAnimation(timeline); const quint64 tid = e.animation(ti).id;
    Sequencer::Sequence sequence; sequence.name = "Main";
    Sequencer::Step first, second; first.snapshot = sid; first.text = "Intro"; first.preWait = 1;
    second.preset = pid; second.description = "Then the look"; sequence.steps = {first, second};
    const int qi = e.sequencer().add(sequence); const quint64 qid = e.sequencer().sequenceId(qi);
    e.sequencer().setCurrent(qi);
    const quint64 cueId = e.sequencer().sequence(qi).steps[1].runtimeId;
    auto data = get("/show/catalog");
    check(data.value("version").toInt() == 1 && data.value("snapshots").toArray()[0].toObject().value("id").toString() == QString::number(sid),
          "catalog IDs are strings and schema is versioned");
    const auto cues = data.value("sequences").toArray()[0].toObject().value("cues").toArray();
    check(cues.size() == 2 && cues[0].toObject().value("text") == "Intro" && cues[1].toObject().value("target_type") == "preset" &&
          cues[1].toObject().value("id").toString() == QString::number(cueId), "catalog lists all cue metadata with runtime identity");
    const QString snapshotPath = QStringLiteral("/snapshot/by-id/%1/recall").arg(sid);
    e.addSnapshot(snapshot, 0); // identical labels still change the structure identity
    e.setParameter(layer, "opacity", .9);
    check(server.handleMessage({snapshotPath, "", {}}) && std::abs(e.parameterValue(layer, "opacity").toDouble() - .2) < 1e-6,
          "stable snapshot address survives insertion with an identical name");
    check(!server.handleMessage({snapshotPath, "i", {0}}), "trigger releases do not recall a second time");
    check(server.handleMessage({QStringLiteral("/preset/by-id/%1/recall").arg(pid), "", {}}) &&
          std::abs(e.parameterValue(layer, "opacity").toDouble() - .7) < 1e-6, "stable preset recall applies the requested preset");
    const QString sequencePath = QStringLiteral("/sequence/by-id/%1").arg(qid);
    const QString oldSignature = OscNamespace(&e).signature();
    auto edited = e.sequencer().sequence(e.sequencer().indexOfSequence(qid));
    std::reverse(edited.steps.begin(), edited.steps.end()); edited.steps[0].text = "Moved preset cue";
    e.sequencer().setSequence(e.sequencer().indexOfSequence(qid), edited);
    check(OscNamespace(&e).signature() != oldSignature, "cue edits invalidate the OSCQuery catalog");
    check(server.handleMessage({sequencePath + "/cue/go", "s", {QString::number(cueId)}}) && e.sequencer().position() == 0,
          "cue-ID commands follow a cue after reordering");
    e.sequencer().add(Sequencer::Sequence{}, 0);
    check(server.handleMessage({sequencePath + "/select", "", {}}) && e.sequencer().current() == 1,
          "sequence selection uses identity instead of old list index");
    check(server.handleMessage({sequencePath + "/go", "", {}}), "stable GO advances the selected sequence");
    data = get("/show/state");
    const auto values = data.value("values").toObject();
    check(values.value("/sequence/id").toString() == QString::number(qid) && values.value("/sequence/cue/current/number").toInt() == 2 &&
          values.value("/sequence/cue/current/phase") == "pre-wait", "live state includes cue roles and timing phases");
    check(server.handleMessage({sequencePath + "/stop", "", {}}) && !e.sequencer().running(), "stable STOP cancels pending waits");
    const QString timelinePath = QStringLiteral("/timeline/by-id/%1").arg(tid);
    check(server.handleMessage({timelinePath + "/play", "", {}}), "stable timeline transport starts playback");
    e.advanceFades(.5);
    const auto playing = get("/show/state").value("timelines").toArray()[0].toObject();
    check(playing.value("state") == "playing" && std::abs(playing.value("position").toDouble() - .5) < 1e-6 &&
          std::abs(playing.value("remaining").toDouble() - 1.5) < 1e-6, "timeline feedback reports position, duration and remaining time");
    e.removeAnimation(e.indexOfAnimation(tid));
    check(!server.handleMessage({timelinePath + "/play", "", {}}), "removed timeline IDs are refused");
    e.removeSnapshot(e.indexOfSnapshot(sid));
    check(!server.handleMessage({snapshotPath, "", {}}), "removed snapshot IDs cannot launch another snapshot");
    e.newProject();
    return failures;
}
