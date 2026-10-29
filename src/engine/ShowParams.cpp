// The show's own actions as the composition's parameters: GO, a snapshot or a preset recalled, a timeline played,
// the blackout, the pause — so that an interaction (a key, a MIDI note, another value of the show) or an event of a
// timeline reaches them as it reaches any value, without OSC in between. Made again when the snapshots, the presets,
// the timelines or the sequences change (each is addressed by its stable id). Never saved: they are actions, or
// states of the show that a cue does not hold.
//
// A parameter is written with the engine's lock held, from the interface, a timeline (the render thread) or an
// interaction: what it does runs just after, on the engine's thread, as a button of the interface would.
#include "EngineInternal.h"

namespace {
QString named(const QString &name, quint64 id) { return name.isEmpty() ? QString::number(id) : name; }
} // namespace

void Engine::later(std::function<void()> f)
{
    QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection);
}

const std::vector<std::unique_ptr<Parameter>> &Engine::showParams()
{
    // What they are made from: the ids and names of what they act on
    QString key = QString::number(m_sequencer->revision());
    for (const Snapshot &s : m_snapshots) key += QStringLiteral("|s%1:%2").arg(s.id).arg(s.name);
    for (const Preset &p : m_presets) key += QStringLiteral("|p%1:%2").arg(p.id).arg(p.name);
    for (const Animation &a : m_animations) key += QStringLiteral("|t%1:%2").arg(a.id).arg(a.name);
    if (key == m_showParamsKey && !m_showParams.empty()) return m_showParams;
    m_showParamsKey = key;
    m_showParams.clear();
    using T = Parameter::Type;
    auto add = [this](const QString &path, const QString &label, T type) -> Parameter & {
        m_showParams.push_back(std::make_unique<Parameter>(path, label, type));
        return *m_showParams.back();
    };
    auto action = [&](const QString &path, const QString &label, const QString &what, std::function<void()> f) {
        add(path, label, T::Trigger).bind(nullptr, [this, what, f](const QVariant &) {
            journal(QStringLiteral("show"), what);
            later(f);
        });
    };

    // The blackout (its own fade) and the pause, as the transport bar's switches
    add("show/blackout", "Show › Blackout", T::Bool)
        .ramp(Parameter::Ramp::Cut)
        .byDefault(false)
        .transient()
        .bind([this] { return QVariant(m_blackTarget < 0.5); },
              [this](const QVariant &v) {
                  const bool on = v.toBool();
                  journal(QStringLiteral("show"), on ? QStringLiteral("blackout on") : QStringLiteral("blackout off"));
                  later([this, on] { setBlackout(on); });
              });
    add("show/pause", "Show › Pause", T::Bool)
        .ramp(Parameter::Ramp::Cut)
        .byDefault(false)
        .transient()
        .bind([this] { return QVariant(m_paused.load()); },
              [this](const QVariant &v) {
                  const bool on = v.toBool();
                  journal(QStringLiteral("show"), on ? QStringLiteral("pause on") : QStringLiteral("pause off"));
                  later([this, on] { setPaused(on); });
              });

    // The selected sequence, as the GO bar
    action("show/go", "Show › GO", QStringLiteral("go"), [this] { m_sequencer->go(); });
    action("show/back", "Show › GO Back", QStringLiteral("go back"), [this] { m_sequencer->back(); });
    action("show/stop", "Show › Stop", QStringLiteral("stop"), [this] { m_sequencer->stop(); });
    action("show/stop_all", "Show › Stop All", QStringLiteral("stop all"), [this] { m_sequencer->stopAll(); });

    // Each sequence, whatever is selected
    for (int i = 0; i < m_sequencer->count(); ++i) {
        const quint64 id = m_sequencer->sequenceId(i);
        const QString base = QStringLiteral("sequence/%1/").arg(id);
        const QString cat = QStringLiteral("Sequence · %1 › ").arg(named(m_sequencer->sequence(i).name, i + 1));
        action(base + "go", cat + "GO", QStringLiteral("sequence %1 go").arg(id), [this, id] { m_sequencer->goById(id); });
        action(base + "back", cat + "GO Back", QStringLiteral("sequence %1 back").arg(id),
               [this, id] { m_sequencer->backById(id); });
        action(base + "stop", cat + "Stop", QStringLiteral("sequence %1 stop").arg(id),
               [this, id] { m_sequencer->stopById(id); });
    }

    // Each snapshot and each preset, recalled (with its fade)
    for (const Snapshot &s : m_snapshots)
        action(QStringLiteral("snapshot/%1/recall").arg(s.id), QStringLiteral("Snapshots › %1").arg(named(s.name, s.id)),
               QStringLiteral("snapshot %1 recall").arg(s.id), [this, id = s.id] { recallSnapshotById(id); });
    for (const Preset &p : m_presets)
        action(QStringLiteral("preset/%1/recall").arg(p.id), QStringLiteral("Presets › %1").arg(named(p.name, p.id)),
               QStringLiteral("preset %1 recall").arg(p.id), [this, id = p.id] {
                   const int i = indexOfPreset(id);
                   if (i >= 0) recallPreset(i);
               });

    // Each timeline: its transport, and whether it plays (for an Internal interaction to follow)
    for (const Animation &a : m_animations) {
        const QString base = QStringLiteral("timeline/%1/").arg(a.id);
        const QString cat = QStringLiteral("Timeline · %1 › ").arg(named(a.name, a.id));
        const quint64 id = a.id;
        const std::pair<const char *, AnimAction> transport[] = {
            {"play", AnimAction::Play}, {"pause", AnimAction::Pause}, {"stop", AnimAction::Stop}, {"rewind", AnimAction::Rewind}};
        const char *const labels[] = {"Play", "Pause", "Stop", "Rewind"};
        for (int k = 0; k < 4; ++k) {
            const AnimAction act = transport[k].second;
            action(base + transport[k].first, cat + labels[k], QStringLiteral("timeline %1 %2").arg(id).arg(transport[k].first),
                   [this, id, act] { controlAnimation(id, act); });
        }
        add(base + "playing", cat + "Playing", T::Bool).readOnly().bind([this, id] {
            for (const Animation &x : m_animations)
                if (x.id == id) return QVariant(x.state == AnimState::Playing);
            return QVariant(false);
        }, nullptr);
    }
    return m_showParams;
}

bool Engine::isShowParam(const QString &path)
{
    for (const char *prefix : {"show/", "sequence/", "snapshot/", "preset/", "timeline/"})
        if (path.startsWith(QLatin1String(prefix))) return true;
    return false;
}
