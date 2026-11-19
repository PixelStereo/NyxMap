// The show's controls (ShowControl.h): their list, and each as a parameter of the show (control/<key>, ShowParams.cpp).
// The list's logic (keys, groups, values, views of a parameter) is pxs's (pxs/ControlList.h); here are the engine's lock, its signals,
// the keys' effect on the show (remapControlKeys) and the parameters a view drives.
#include "EngineInternal.h"
#include "ParameterKey.h"
#include "ShowControl.h"

#include <QJsonDocument>
#include <pxs/ControlList.h>

namespace {
constexpr int kMaxControls = pxs::controls::kMaxControls;

QString controlKey(const QString &name, const QStringList &taken, int index) { return faustControlKey(name, taken, index); }
} // namespace

// The parameters of the show, as the views of a parameter see them (lock held by the caller)
class EngineParameterSource final : public pxs::controls::ParameterSource
{
public:
    explicit EngineParameterSource(Engine *e) : m_e(e) {}
    bool info(quint64 scope, const QString &path, ParamInfo *out) const override { return m_e->parameterInfo(scope, path, out); }
    QJsonValue value(quint64 scope, const QString &path) const override
    {
        const Parameter *p = m_e->findParameter(scope, path);
        return p ? p->json() : QJsonValue(QJsonValue::Undefined);
    }
    bool setNumber(quint64 scope, const QString &path, double v) override
    {
        Parameter *p = m_e->findParameter(scope, path);
        return p && p->setNumber(v);
    }
    bool setJson(quint64 scope, const QString &path, const QJsonValue &v) override
    {
        Parameter *p = m_e->findParameter(scope, path);
        return p && p->setJson(v);
    }
    void touched(quint64 scope, const QString &path) override
    {
        if (scope) m_e->releaseFromFades(scope, path);
    }

    // What a value set on a control is for the show: its address, control/<key>[/<address>]
    static pxs::controls::Feed feed(Engine *e, const QString &key)
    {
        return [e, key](const QString &address, double v) {
            e->feedControlAction(QStringLiteral("control/") + key + (address.isEmpty() ? QString() : QLatin1Char('/') + address), v);
        };
    }

private:
    Engine *m_e;
};

std::vector<ShowControl> Engine::controls() const
{
    Lock lk(&m_mutex);
    std::vector<ShowControl> list = m_controls;
    EngineParameterSource source(const_cast<Engine *>(this));
    for (ShowControl &c : list)
        if (!c.boundParam.isEmpty()) pxs::controls::fillBound(c, source);
    return list;
}

QJsonArray Engine::controlsJson() const
{
    Lock lk(&m_mutex);
    QJsonArray a;
    for (const ShowControl &c : m_controls) a.append(c.toJson());
    return a;
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
    pxs::controls::keyAll(next, controlKey);
    pxs::controls::tidyGroups(next);
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
        id = pxs::controls::add(m_controls, m_nextControlId, control, controlKey);
        if (!id) return 0;
        ++m_controlsRevision;
    }
    emit controlsChanged();
    return id;
}

bool Engine::setControlLayouts(const QHash<quint64, std::array<int, 4>> &places)
{
    bool changed = false;
    {
        Lock lk(&m_mutex);
        changed = pxs::controls::setLayouts(m_controls, places);
        if (changed) ++m_controlsRevision;
    }
    if (changed) emit controlsChanged();
    return changed;
}

quint64 Engine::addControlForParameter(quint64 layer, const QString &path, quint64 group)
{
    EngineParameterSource source(this);
    const std::optional<ShowControl> c = pxs::controls::forParameter(source, layer, path, group);
    return c ? addControl(*c) : 0;
}

quint64 Engine::driveParameterFromControl(quint64 layer, const QString &path, quint64 controlId, const QString &sub)
{
    ParamInfo info;
    if (!parameterInfo(layer, path, &info) || !(info.access & ParamInfo::Write) || !info.isNumber()) return 0;
    const std::optional<ShowControl> control = this->control(controlId);
    if (!control) return 0;
    const auto sources = control->numberSources();
    if (std::none_of(sources.begin(), sources.end(), [&](const auto &s) { return s.second == sub; })) return 0;
    Interaction it = Interaction::made(InteractionSource::Internal);
    it.sourceLayer = 0;
    it.sourceParam = QStringLiteral("control/") + control->key + (sub.isEmpty() ? QString() : QLatin1Char('/') + sub);
    it.layer = layer;
    it.param = path;
    it.takeover = Takeover::Jump;
    const double lo = control->kind == ShowControl::Kind::Adsr ? 0.0 : control->min, hi = control->kind == ShowControl::Kind::Adsr
                                                                                                ? (sub == QLatin1String("sustain") ? 1.0 : control->max)
                                                                                                : control->max;
    if (lo != info.min || hi != info.max) it.filters = {InteractionStage::remap(lo, hi, info.min, info.max)};
    return addInteraction(it);
}

bool Engine::updateControl(quint64 id, const ShowControl &change)
{
    bool keyChanged = false;
    {
        Lock lk(&m_mutex);
        const pxs::controls::Updated u = pxs::controls::update(m_controls, id, change, controlKey);
        if (!u.found) return false;
        keyChanged = u.keyChanged;
        if (keyChanged) remapControlKeys({{u.oldKey, u.newKey}});
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
        if (!pxs::controls::remove(m_controls, id)) return false;
        ++m_controlsRevision;
    }
    emit controlsChanged();
    return true;
}

bool Engine::moveControl(quint64 id, int to)
{
    {
        Lock lk(&m_mutex);
        if (!pxs::controls::move(m_controls, id, to)) return false;
        ++m_controlsRevision;
    }
    emit controlsChanged();
    return true;
}

bool Engine::setControlValue(quint64 id, double value)
{
    Lock lk(&m_mutex);
    ShowControl *c = pxs::controls::find(m_controls, id);
    if (!c || c->kind == ShowControl::Kind::Color || c->kind == ShowControl::Kind::Group || !std::isfinite(value)) return false;
    if (!c->boundParam.isEmpty()) { // a view of a parameter: the parameter is what is set
        EngineParameterSource source(this);
        return pxs::controls::setBound(*c, source, value);
    }
    return pxs::controls::setValue(m_controls, id, value, EngineParameterSource::feed(this, c->key));
}

void Engine::setMousePosition(double x, double y)
{
    Lock lk(&m_mutex);
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    m_mouseX = std::clamp(x, 0.0, 1.0);
    m_mouseY = std::clamp(y, 0.0, 1.0);
}

bool Engine::setControlPoint(quint64 id, double x, double y)
{
    Lock lk(&m_mutex);
    const ShowControl *c = pxs::controls::find(m_controls, id);
    return c && pxs::controls::setPoint(m_controls, id, x, y, EngineParameterSource::feed(this, c->key));
}

bool Engine::setControlPoints(quint64 id, const std::vector<double> &xy)
{
    Lock lk(&m_mutex);
    const ShowControl *c = pxs::controls::find(m_controls, id);
    return c && pxs::controls::setPoints(m_controls, id, xy, EngineParameterSource::feed(this, c->key));
}

bool Engine::setControlMulti(quint64 id, const std::vector<double> &values)
{
    Lock lk(&m_mutex);
    const ShowControl *c = pxs::controls::find(m_controls, id);
    return c && pxs::controls::setMulti(m_controls, id, values, EngineParameterSource::feed(this, c->key));
}

bool Engine::setControlAdsr(quint64 id, const std::array<double, 4> &adsr)
{
    Lock lk(&m_mutex);
    const ShowControl *c = pxs::controls::find(m_controls, id);
    return c && pxs::controls::setAdsr(m_controls, id, adsr, EngineParameterSource::feed(this, c->key));
}

bool Engine::setControlColor(quint64 id, double r, double g, double b)
{
    Lock lk(&m_mutex);
    ShowControl *c = pxs::controls::find(m_controls, id);
    if (!c || c->kind != ShowControl::Kind::Color) return false;
    if (!c->boundParam.isEmpty()) { // a view of a color: its three channels
        EngineParameterSource source(this);
        return pxs::controls::setBoundColor(*c, source, r, g, b);
    }
    return pxs::controls::setColor(*c, r, g, b, EngineParameterSource::feed(this, c->key));
}

std::optional<ShowControl> Engine::control(quint64 id) const
{
    Lock lk(&m_mutex);
    const ShowControl *c = pxs::controls::find(m_controls, id);
    if (!c) return std::nullopt;
    ShowControl out = *c;
    if (!out.boundParam.isEmpty()) {
        EngineParameterSource source(const_cast<Engine *>(this));
        pxs::controls::fillBound(out, source);
    }
    return out;
}

// A control's parameters, by its id (the lookups of the bindings: its place in the list changes)
ShowControl *Engine::controlLocked(quint64 id) { return pxs::controls::find(m_controls, id); }
