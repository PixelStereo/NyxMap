#include "ShowDesk.h"
#include "Engine.h"
#include "SequenceFeedback.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace {
QString idText(quint64 id) { return QString::number(id); }
double finiteTime(double seconds) { return std::isfinite(seconds) ? seconds : -1; }
quint64 idOf(const QJsonObject &o, const char *key) { return o.value(QLatin1String(key)).toString().toULongLong(); }
}

namespace showdesk {
QJsonObject catalog(Engine *e)
{
    Engine::Lock lock(&e->mutex());
    QJsonArray snapshots, presets, timelines, sequences, layers;
    auto item = [](quint64 id, int index, const QString &name) {
        return QJsonObject{{"id", idText(id)}, {"index", index + 1}, {"name", name}};
    };
    for (int k = 0; k < e->layerCount(); ++k) {
        const Layer *layer = e->layer(k);
        auto entry = item(layer->id, k, layer->name);
        entry["parent"] = idText(layer->parent);
        entry["group"] = layer->isGroup;
        entry["viewport"] = layer->isViewport;
        entry["path"] = "/show/layer/by-id/" + idText(layer->id);
        layers.append(entry);
    }
    for (int k = 0; k < e->snapshotCount(); ++k) {
        const auto value = e->snapshot(k);
        snapshots.append(item(value.id, k, value.name));
    }
    for (int k = 0; k < e->presetCount(); ++k) {
        const auto value = e->preset(k);
        presets.append(item(value.id, k, value.name));
    }
    for (int k = 0; k < e->animationCount(); ++k) {
        const auto value = e->animation(k);
        auto entry = item(value.id, k, value.name);
        entry["duration"] = value.duration;
        timelines.append(entry);
    }
    auto &sq = e->sequencer();
    for (int k = 0; k < sq.count(); ++k) {
        const auto value = sq.sequence(k);
        auto entry = item(sq.sequenceId(k), k, value.name);
        entry["loop"] = value.loop;
        entry["edit_mode"] = value.editMode;
        QJsonArray cues;
        for (size_t n = 0; n < value.steps.size(); ++n) {
            const auto &step = value.steps[n];
            QString kind = "empty", targetName;
            quint64 target = 0;
            int index = -1;
            if (!step.actions.empty()) {
                kind = "group";
                targetName = QStringLiteral("%1 actions").arg(step.actions.size());
            } else if (step.sequence) {
                kind = "sequence"; target = step.sequence; index = sq.indexOfSequence(target);
                if (index >= 0) targetName = sq.sequence(index).name;
            } else if (step.preset) {
                kind = "preset"; target = step.preset; index = e->indexOfPreset(target);
                if (index >= 0) targetName = e->preset(index).name;
            } else if (step.timeline) {
                kind = "timeline"; target = step.timeline; index = e->indexOfAnimation(target);
                if (index >= 0) targetName = e->animation(index).name;
            } else if (step.snapshot) {
                kind = "snapshot"; target = step.snapshot; index = e->indexOfSnapshot(target);
                if (index >= 0) targetName = e->snapshot(index).name;
            }
            auto cue = item(step.runtimeId, int(n), targetName);
            if (!step.actions.empty()) {
                QJsonArray actions;
                for (const auto &action : step.actions)
                    actions.append(Sequencer::stepToJson(action));
                cue["actions"] = actions;
            }
            cue["text"] = step.text; cue["description"] = step.description;
            cue["target_type"] = kind; cue["target_id"] = idText(target); cue["missing"] = target && index < 0;
            cue["prewait"] = step.preWait; cue["postwait"] = step.postWait;
            cue["action_duration"] = finiteTime(sq.stepDuration(step));
            cue["continue"] = step.next == Sequencer::StepContinue::Follow ? "follow"
                            : step.next == Sequencer::StepContinue::AutoFollow ? "auto-follow" : "wait";
            cues.append(cue);
        }
        entry["cues"] = cues;
        sequences.append(entry);
    }
    return {{"version", 1}, {"snapshots", snapshots}, {"presets", presets}, {"timelines", timelines}, {"sequences", sequences}, {"layers", layers}};
}

QJsonObject state(Engine *e)
{
    Engine::Lock lock(&e->mutex());
    QJsonObject values;
    for (const auto &message : SequenceFeedback::stateFor(e)) {
        if (message.address == QLatin1String("/sequence/modified")) continue; // GUI-only edit indicator
        values[message.address] = QJsonValue::fromVariant(message.args.value(0));
    }
    auto &sq = e->sequencer();
    values["/sequence/id"] = idText(sq.sequenceId(sq.current()));
    values["/composition/pause"] = e->paused();
    values["/composition/blackout"] = e->blackout();
    values["/composition/speed"] = e->compositionSpeed();
    QJsonArray timelines, sequences;
    std::vector<Engine::AnimationPlayback> playback;
    e->sampleAnimations(&playback); // small telemetry; never copies keyframes
    for (const auto &a : playback) {
        const double remaining = std::isfinite(a.length) ? std::max(0.0, a.length - a.clock) : -1;
        timelines.append(QJsonObject{{"id", idText(a.id)},
            {"state", a.state == AnimState::Playing ? "playing" : a.state == AnimState::Paused ? "paused" : "stopped"},
            {"position", a.position}, {"duration", a.duration}, {"elapsed", a.clock}, {"remaining", remaining},
            {"progress", a.duration > 0 ? std::clamp(a.position / a.duration, 0.0, 1.0) : 0},
            {"pass", a.pass}, {"speed", a.speed}});
    }
    for (int k = 0; k < sq.count(); ++k)
        sequences.append(QJsonObject{{"id", idText(sq.sequenceId(k))}, {"active", sq.active(k)}});
    return {{"version", 1}, {"values", values}, {"timelines", timelines}, {"sequences", sequences}};
}

bool command(Engine *e, const QJsonObject &o)
{
    const QString c = o.value("c").toString();
    auto &sq = e->sequencer();
    if (c == QLatin1String("stop_all")) {
        sq.stopAll();
        return true;
    }
    if (c == QLatin1String("go")) return sq.goById(idOf(o, "id"));
    if (c == QLatin1String("back")) return sq.backById(idOf(o, "id"));
    if (c == QLatin1String("stop")) return sq.stopById(idOf(o, "id"));
    if (c == QLatin1String("select")) return sq.selectById(idOf(o, "id"));
    if (c == QLatin1String("cue")) return sq.cueById(idOf(o, "id"), idOf(o, "cue"));
    if (c == QLatin1String("snapshot")) {
        const quint64 id = idOf(o, "id");
        if (sq.requestSnapshot(id)) return true;
        const int index = e->indexOfSnapshot(id);
        if (index < 0) return false;
        e->recallSnapshot(index);
        return true;
    }
    if (c == QLatin1String("preset")) {
        const int index = e->indexOfPreset(idOf(o, "id"));
        if (index < 0) return false;
        e->recallPreset(index);
        return true;
    }
    if (c == QLatin1String("timeline")) {
        const quint64 id = idOf(o, "id");
        if (e->indexOfAnimation(id) < 0) return false;
        const QString action = o.value("do").toString();
        if (action == QLatin1String("play")) e->controlAnimation(id, AnimAction::Play);
        else if (action == QLatin1String("pause")) e->controlAnimation(id, AnimAction::Pause);
        else if (action == QLatin1String("stop")) e->controlAnimation(id, AnimAction::Stop);
        else if (action == QLatin1String("rewind")) e->controlAnimation(id, AnimAction::Rewind);
        else return false;
        return true;
    }
    return false;
}
} // namespace showdesk
