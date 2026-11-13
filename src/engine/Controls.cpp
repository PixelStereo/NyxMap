// The show's controls (ShowControl.h): their list, and each as a parameter of the show (control/<key>, ShowParams.cpp).
#include "EngineInternal.h"
#include "ParameterKey.h"
#include "ShowControl.h"

#include <QJsonDocument>

namespace {
constexpr int kMaxControls = 256;
} // namespace

std::vector<ShowControl> Engine::controls() const
{
    Lock lk(&m_mutex);
    return m_controls;
}

QJsonArray Engine::controlsJson() const
{
    Lock lk(&m_mutex);
    QJsonArray a;
    for (const ShowControl &c : m_controls) a.append(c.toJson());
    return a;
}

// Names to unique keys: "Master Level" is control/master_level, then control/master_level_2.
static void keyAll(std::vector<ShowControl> &list)
{
    QStringList taken;
    for (const ShowControl &c : list)
        if (!c.key.isEmpty()) taken << c.key;
    int index = 0;
    for (ShowControl &c : list) {
        ++index;
        if (!c.key.isEmpty()) continue;
        QStringList others = taken;
        c.key = faustControlKey(c.name, others, index);
        taken << c.key;
    }
}

// A control's group must be a Group of the list that is not itself nor inside it; a span and columns stay within 1..4
static void tidyGroups(std::vector<ShowControl> &list)
{
    const auto find = [&](quint64 id) -> const ShowControl * {
        for (const ShowControl &c : list)
            if (c.id == id) return &c;
        return nullptr;
    };
    for (ShowControl &c : list) {
        c.span = std::clamp(c.span, 1, 4);
        c.columns = std::clamp(c.columns, 1, 4);
        const ShowControl *g = c.group ? find(c.group) : nullptr;
        bool ok = g && g->kind == ShowControl::Kind::Group && g->id != c.id;
        // Up the chain: it must not come back to this one
        for (int depth = 0; ok && g && g->group && depth < 64; ++depth) {
            if (g->group == c.id) ok = false;
            g = find(g->group);
        }
        if (!ok) c.group = 0;
    }
}

void Engine::remapControlKeys(const QHash<QString, QString> &keys)
{
    if (keys.isEmpty()) return;
    const QString prefix = QStringLiteral("control/");
    const auto remap = [&](quint64 layer, QString &path) {
        if (layer || !path.startsWith(prefix)) return;
        const auto slash = path.indexOf(QLatin1Char('/'), prefix.size());
        const auto key = keys.constFind(path.mid(prefix.size(), slash < 0 ? -1 : slash - prefix.size()));
        if (key != keys.cend()) path = prefix + key.value() + (slash < 0 ? QString() : path.mid(slash));
    };
    const auto interaction = [&](Interaction &entry) {
        const auto binding = [&](Interaction &item) {
            if (item.source == InteractionSource::Internal) remap(item.sourceLayer, item.sourceParam);
            remap(item.layer, item.param);
            for (auto *actions : {&item.onTrue, &item.onFalse})
                for (auto &action : *actions) remap(action.layer, action.param);
        };
        binding(entry);
        for (auto &axis : entry.xyInputs) binding(axis);
    };
    const auto timeline = [&](Animation &entry) {
        for (auto &track : entry.tracks) {
            remap(track.layer, track.param);
            for (auto &axis : track.axes) remap(axis.layer, axis.param);
        }
    };
    const auto remapGraph = [&](InteractionGraph &graph) {
        for (auto &node : graph.nodes) interaction(node.settings);
    };
    const auto storedValue = [&](const QString &path, QJsonValue value) {
        QString text = value.toString();
        if ((path == "source/internal" || path == "target") && text.startsWith(QLatin1String("0:"))) {
            text.remove(0, 2);
            remap(0, text);
            return QJsonValue(QStringLiteral("0:") + text);
        }
        if (path == "graph") {
            InteractionGraph graph;
            if (InteractionGraph::fromJson(QJsonDocument::fromJson(text.toUtf8()).object(), &graph)) {
                remapGraph(graph);
                return QJsonValue(QString::fromUtf8(QJsonDocument(graph.toJson()).toJson(QJsonDocument::Compact)));
            }
        }
        return value;
    };
    for (auto &entry : m_interactions) interaction(entry);
    m_interactionParamsDirty = true;
    for (auto &fade : m_actionFades) remap(fade.layer, fade.path);
    for (auto &entry : m_animations) timeline(entry);
    for (auto &preset : m_presets) {
        for (auto &entry : preset.interactions) interaction(entry);
        for (auto &entry : preset.animations) timeline(entry);
        for (auto &value : preset.values) {
            remap(value.layer, value.path);
            if (!value.layer && value.path.startsWith(QLatin1String("interaction/")))
                value.value = storedValue(value.path.section(QLatin1Char('/'), 2), value.value);
        }
    }
    for (auto &snapshot : m_snapshots) {
        for (qsizetype i = 0; i < snapshot.interactions.size(); ++i) {
            auto entry = snapshot.interactions.at(i).toObject();
            if (entry.contains("definition")) {
                auto definition = Interaction::fromJson(entry.value("definition").toObject());
                interaction(definition);
                entry["definition"] = definition.toJson();
            }
            auto params = entry.value("params").toObject();
            for (const char *path : {"source/internal", "target", "graph"})
                if (params.contains(path)) params[path] = storedValue(QLatin1String(path), params.value(path));
            entry["params"] = params;
            snapshot.interactions[i] = entry;
        }
    }
    if (!m_interactionGraph.nodes.empty()) {
        remapGraph(m_interactionGraph);
        m_graphProcessor.configure(m_interactionGraph);
        ++m_graphRevision;
        m_graphLastOutputs.clear();
    }
    std::lock_guard<std::mutex> lock(m_interactionEventsMutex);
    for (auto &event : m_interactionEvents)
        if (event.source == InteractionSource::Internal) remap(0, event.address);
}

bool Engine::setControlsJson(const QJsonArray &a, QString *error, bool remapKeys)
{
    if (a.size() > kMaxControls) {
        if (error) *error = QStringLiteral("At most %1 controls").arg(kMaxControls);
        return false;
    }
    std::vector<ShowControl> next;
    quint64 highest = 0;
    QStringList seen;
    for (const QJsonValue &v : a) {
        if (!v.isObject()) {
            if (error) *error = QStringLiteral("Invalid control");
            return false;
        }
        ShowControl c = ShowControl::fromJson(v.toObject());
        if (c.key.contains(QLatin1Char('/')) || seen.contains(c.key)) c.key.clear(); // a hand-edited duplicate: a new key
        if (!c.key.isEmpty()) seen << c.key;
        highest = std::max(highest, c.id);
        next.push_back(std::move(c));
    }
    // Ids: those saved, a new one for what has none or repeats one
    QSet<quint64> ids;
    for (ShowControl &c : next) {
        if (!c.id || ids.contains(c.id)) c.id = 0;
        else ids.insert(c.id);
    }
    quint64 id = std::max(highest, m_nextControlId - 1);
    for (ShowControl &c : next)
        if (!c.id) c.id = ++id;
    keyAll(next);
    tidyGroups(next);
    {
        Lock lk(&m_mutex);
        if (remapKeys) {
            QHash<QString, QString> keys;
            for (const auto &control : next)
                if (const auto *old = controlLocked(control.id); old && old->key != control.key)
                    keys.insert(old->key, control.key);
            remapControlKeys(keys);
        }
        m_controls = std::move(next);
        m_nextControlId = std::max(m_nextControlId, id + 1);
        ++m_controlsRevision;
    }
    emit controlsChanged();
    if (remapKeys) {
        emit interactionsChanged();
        emit animationsChanged();
        emit presetsChanged();
        emit snapshotsChanged();
    }
    return true;
}

quint64 Engine::addControl(const ShowControl &control)
{
    quint64 id = 0;
    {
        Lock lk(&m_mutex);
        if (int(m_controls.size()) >= kMaxControls) return 0;
        ShowControl c = control;
        c.id = m_nextControlId++;
        c.key.clear();
        if (c.name.trimmed().isEmpty()) c.name = ShowControl::kindName(c.kind);
        c.value = c.kind == ShowControl::Kind::Slider ? std::clamp(c.value, std::min(c.min, c.max), std::max(c.min, c.max)) : c.clamped(c.value);
        if (c.kind == ShowControl::Kind::Pad) {
            c.value2 = c.clamped(c.value2);
            c.def = std::clamp(c.def, c.min, c.max);
        }
        if (c.kind == ShowControl::Kind::Choice && c.choices.isEmpty()) c.choices = {QStringLiteral("A"), QStringLiteral("B")};
        id = c.id;
        m_controls.push_back(std::move(c));
        keyAll(m_controls);
        tidyGroups(m_controls);
        ++m_controlsRevision;
    }
    emit controlsChanged();
    return id;
}

bool Engine::updateControl(quint64 id, const ShowControl &change)
{
    bool keyChanged = false;
    {
        Lock lk(&m_mutex);
        auto it = std::find_if(m_controls.begin(), m_controls.end(), [id](const ShowControl &c) { return c.id == id; });
        if (it == m_controls.end()) return false;
        ShowControl c = change;
        c.id = id;
        c.key = it->key;
        c.name = c.name.trimmed();
        if (c.name.isEmpty()) c.name = it->name;
        if (c.name != it->name) {
            QStringList taken;
            for (const auto &other : m_controls)
                if (other.id != id) taken << other.key;
            c.key = faustControlKey(c.name, taken, int(it - m_controls.begin()) + 1);
            keyChanged = c.key != it->key;
            if (keyChanged) remapControlKeys({{it->key, c.key}});
        }
        if (c.kind == ShowControl::Kind::Slider || c.kind == ShowControl::Kind::Pad) {
            if (!(c.max > c.min)) c.max = c.min + 1;
            c.def = std::clamp(c.def, c.min, c.max);
        }
        if (c.kind == ShowControl::Kind::Choice && c.choices.isEmpty()) c.choices = it->choices;
        c.value = c.kind == it->kind ? c.clamped(c.value) : (c.kind == ShowControl::Kind::Slider || c.kind == ShowControl::Kind::Pad ? c.def : 0.0);
        c.value2 = c.kind == ShowControl::Kind::Pad ? (c.kind == it->kind ? c.clamped(c.value2) : c.def) : 0.0;
        *it = c;
        tidyGroups(m_controls);
        ++m_controlsRevision;
    }
    emit controlsChanged();
    if (keyChanged) {
        emit interactionsChanged();
        emit animationsChanged();
        emit presetsChanged();
        emit snapshotsChanged();
    }
    return true;
}

bool Engine::removeControl(quint64 id)
{
    {
        Lock lk(&m_mutex);
        auto it = std::find_if(m_controls.begin(), m_controls.end(), [id](const ShowControl &c) { return c.id == id; });
        if (it == m_controls.end()) return false;
        const quint64 parent = it->group;
        m_controls.erase(it);
        for (ShowControl &c : m_controls) // what a group held goes where the group was
            if (c.group == id) c.group = parent;
        ++m_controlsRevision;
    }
    emit controlsChanged();
    return true;
}

bool Engine::moveControl(quint64 id, int to)
{
    {
        Lock lk(&m_mutex);
        auto it = std::find_if(m_controls.begin(), m_controls.end(), [id](const ShowControl &c) { return c.id == id; });
        if (it == m_controls.end()) return false;
        const int from = int(it - m_controls.begin());
        to = std::clamp(to, 0, int(m_controls.size()) - 1);
        if (from == to) return true;
        ShowControl c = *it;
        m_controls.erase(it);
        m_controls.insert(m_controls.begin() + to, std::move(c));
        ++m_controlsRevision;
    }
    emit controlsChanged();
    return true;
}

bool Engine::setControlValue(quint64 id, double value)
{
    Lock lk(&m_mutex);
    auto it = std::find_if(m_controls.begin(), m_controls.end(), [id](const ShowControl &c) { return c.id == id; });
    if (it == m_controls.end() || it->kind == ShowControl::Kind::Color || it->kind == ShowControl::Kind::Group || !std::isfinite(value)) return false;
    const double next = it->clamped(value);
    if (it->value != next) {
        it->value = next;
        feedControlAction(QStringLiteral("control/") + it->key + (it->kind == ShowControl::Kind::Pad ? QStringLiteral("/x") : QString()), next);
    }
    return true;
}

bool Engine::setControlPoint(quint64 id, double x, double y)
{
    Lock lk(&m_mutex);
    auto it = std::find_if(m_controls.begin(), m_controls.end(), [id](const ShowControl &c) { return c.id == id; });
    if (it == m_controls.end() || it->kind != ShowControl::Kind::Pad) return false;
    const double v[2] = {x, y};
    for (int axis = 0; axis < 2; ++axis) {
        if (!std::isfinite(v[axis])) continue;
        const double next = it->clamped(v[axis]);
        double &field = axis == 0 ? it->value : it->value2;
        if (field == next) continue;
        field = next;
        feedControlAction(QStringLiteral("control/") + it->key + (axis == 0 ? QStringLiteral("/x") : QStringLiteral("/y")), next);
    }
    return true;
}

bool Engine::setControlColor(quint64 id, double r, double g, double b)
{
    Lock lk(&m_mutex);
    auto it = std::find_if(m_controls.begin(), m_controls.end(), [id](const ShowControl &c) { return c.id == id; });
    if (it == m_controls.end() || it->kind != ShowControl::Kind::Color) return false;
    const double c[3] = {r, g, b};
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(c[i])) continue;
        const double next = std::clamp(c[i], 0.0, 1.0);
        if (it->rgb[size_t(i)] == next) continue;
        it->rgb[size_t(i)] = next;
        feedControlAction(QStringLiteral("control/") + it->key + QLatin1Char('/') + QLatin1Char("rgb"[i]), next);
    }
    return true;
}

std::optional<ShowControl> Engine::control(quint64 id) const
{
    Lock lk(&m_mutex);
    for (const ShowControl &c : m_controls)
        if (c.id == id) return c;
    return std::nullopt;
}

// A control's parameters, by its id (the lookups of the bindings: its place in the list changes)
ShowControl *Engine::controlLocked(quint64 id)
{
    for (ShowControl &c : m_controls)
        if (c.id == id) return &c;
    return nullptr;
}
