#include "Osc.h"
#include "Engine.h"
#include "ShowDesk.h"
#include <QJsonDocument>
#include <cmath>

namespace {
QString json(const QJsonObject &value) { return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)); }
QString idText(quint64 id) { return QString::number(id); }
bool trigger(const QVariantList &args) { return args.isEmpty() || !args[0].isValid() || args[0].toDouble() != 0; }
}

void OscNamespace::addShowControl()
{
    Engine *e = m_e;
    add("/show/catalog", "s", 1, "Show catalog (JSON, schema version 1)").get = [e] { return QVariantList{json(showdesk::catalog(e))}; };
    add("/show/state", "s", 1, "Show playback state (JSON, schema version 1)").get = [e] { return QVariantList{json(showdesk::state(e))}; };
    add("/sequence/count", "i", 1, "Sequence count").get = [e] { return QVariantList{e->sequencer().count()}; };
    add("/timeline/count", "i", 1, "Timeline count").get = [e] { return QVariantList{e->animationCount()}; };

    for (int k = 0; k < e->snapshotCount(); ++k) {
        const quint64 id = e->snapshot(k).id;
        add(QStringLiteral("/snapshot/%1/id").arg(k + 1), "s", 1, "Stable snapshot ID").get = [id] { return QVariantList{idText(id)}; };
        add("/snapshot/by-id/" + idText(id) + "/recall", "N", 2, "Recall snapshot").set = [e, id](const QVariantList &args) {
            if (!trigger(args)) return false;
            if (e->sequencer().requestSnapshot(id)) return true;
            const int index = e->indexOfSnapshot(id);
            if (index < 0) return false;
            e->recallSnapshot(index);
            return true;
        };
    }
    for (int k = 0; k < e->presetCount(); ++k) {
        const quint64 id = e->preset(k).id;
        add(QStringLiteral("/preset/%1/id").arg(k + 1), "s", 1, "Stable preset ID").get = [id] { return QVariantList{idText(id)}; };
        add("/preset/by-id/" + idText(id) + "/recall", "N", 2, "Recall preset").set = [e, id](const QVariantList &args) {
            if (!trigger(args)) return false;
            const int index = e->indexOfPreset(id);
            if (index < 0) return false;
            e->recallPreset(index);
            return true;
        };
    }
    for (int k = 0; k < e->animationCount(); ++k) {
        const quint64 id = e->animation(k).id;
        add(QStringLiteral("/timeline/%1/id").arg(k + 1), "s", 1, "Stable timeline ID").get = [id] { return QVariantList{idText(id)}; };
        add(QStringLiteral("/timeline/%1/name").arg(k + 1), "s", 1, "Timeline name").get = [e, id] {
            return QVariantList{e->animation(e->indexOfAnimation(id)).name};
        };
        const QString base = "/timeline/by-id/" + idText(id);
        for (const auto &[word, action] : {std::pair{"play", AnimAction::Play}, {"pause", AnimAction::Pause},
                                         {"stop", AnimAction::Stop}, {"rewind", AnimAction::Rewind}})
            add(base + '/' + word, "N", 2, word).set = [e, id, act = action](const QVariantList &args) {
                if (!trigger(args) || e->indexOfAnimation(id) < 0) return false;
                e->controlAnimation(id, act); return true;
            };
        for (const auto &[word, action] : {std::pair{"seek", AnimAction::Seek}, {"speed", AnimAction::Speed}})
            add(base + '/' + word, "f", 2, word).set = [e, id, act = action](const QVariantList &args) {
                bool ok = false;
                const double value = args.value(0).toDouble(&ok);
                if (!ok || !std::isfinite(value) || e->indexOfAnimation(id) < 0) return false;
                e->controlAnimation(id, act, value); return true;
            };
    }
    auto &sq = e->sequencer();
    for (int k = 0; k < sq.count(); ++k) {
        const quint64 id = sq.sequenceId(k);
        add(QStringLiteral("/sequence/%1/id").arg(k + 1), "s", 1, "Stable sequence ID").get = [id] { return QVariantList{idText(id)}; };
        const QString base = "/sequence/by-id/" + idText(id);
        for (const auto &word : {"go", "back", "stop", "select"})
            add(base + '/' + word, "N", 2, word).set = [e, id, word = QString::fromLatin1(word)](const QVariantList &args) {
                if (!trigger(args)) return false;
                auto &seq = e->sequencer();
                if (word == "go") return seq.goById(id);
                if (word == "back") return seq.backById(id);
                if (word == "stop") return seq.stopById(id);
                return seq.selectById(id);
            };
        add(base + "/cue/go", "s", 2, "GO to cue by runtime ID").set = [e, id](const QVariantList &args) {
            bool ok = false;
            const quint64 cue = args.value(0).toString().toULongLong(&ok);
            return ok && e->sequencer().cueById(id, cue);
        };
    }
}
