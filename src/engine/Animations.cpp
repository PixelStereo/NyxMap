// Timelines (animations): numbers of the layers drawn over time — curves and oscillators — played on their own
// clock; the sequences drive their transport.
#include "EngineInternal.h"
#include "Osc.h"
#include <QUrl>

#include <QJsonArray>
#include <QRandomGenerator>
#include <cmath>
#include <limits>

static constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Values


// The transport (Animation::length, position, pass, setLoop, advance, control…) is pxs-core's (pxs/core/timeline/timeline.h)
static double passDuration(double d) { return std::max(pxs::core::timeline::Transport::kMinDuration, d); }

// ---------------------------------------------------------------------------
// The list

int Engine::animationCount() const
{
    Lock lk(&m_mutex);
    return int(m_animations.size());
}

Engine::Animation Engine::animation(int i) const
{
    Lock lk(&m_mutex);
    return i >= 0 && i < int(m_animations.size()) ? m_animations[size_t(i)] : Animation();
}

int Engine::indexOfAnimation(quint64 id) const
{
    Lock lk(&m_mutex);
    for (size_t i = 0; i < m_animations.size(); ++i)
        if (m_animations[i].id == id) return int(i);
    return -1;
}

int Engine::addAnimation(const Animation &a, int at)
{
    {
        Lock lk(&m_mutex);
        Animation x = a;
        bool taken = !x.id;
        for (const Animation &o : m_animations) taken = taken || o.id == x.id;
        if (taken) x.id = m_nextAnimationId;
        m_nextAnimationId = std::max(m_nextAnimationId, x.id + 1);
        x.state = AnimState::Stopped;
        x.clock = 0;
        x.oscillatorOffset = 0;
        for (AnimTrack &t : x.tracks) sortAnimKeys(t);
        if (at < 0 || at > int(m_animations.size())) at = int(m_animations.size());
        m_animations.insert(m_animations.begin() + at, x);
    }
    emit animationsChanged();
    return at;
}

void Engine::setAnimation(int i, const Animation &a)
{
    {
        Lock lk(&m_mutex);
        if (i < 0 || i >= int(m_animations.size())) return;
        clearAnimationFilterHistory(0, m_animations[size_t(i)].id);
        Animation &x = m_animations[size_t(i)];
        const Animation old = x;
        x = a;
        x.id = old.id;
        // Where it is stays: its clock, its way, the values captured when it started
        x.state = old.state;
        x.clock = old.clock;
        x.oscillatorOffset = old.oscillatorOffset;
        x.reversed = old.reversed;
        x.loop = old.loop;
        x.repeat = old.repeat;
        x.setLoop(a.loop, a.repeat); // a new loop mode: from where it is, without a jump
        x.clock = std::min(x.clock, x.length());
        for (AnimTrack &t : x.tracks) {
            sortAnimKeys(t);
            t.captured = std::numeric_limits<double>::quiet_NaN();
            t.capturedRgb = AnimTrack().capturedRgb;
            for (const AnimTrack &o : old.tracks)
                if (o.layer == t.layer && o.param == t.param && o.color == t.color && o.event == t.event) {
                    t.captured = o.captured;
                    t.capturedRgb = o.capturedRgb;
                    if (t.isPoint() && o.isPoint()) for (int c = 0; c < 2; ++c)
                        t.axes[size_t(c)].captured = o.axes[size_t(c)].captured;
                }
        }
    }
    emit animationsChanged();
}

void Engine::removeAnimation(int i)
{
    {
        Lock lk(&m_mutex);
        if (i < 0 || i >= int(m_animations.size())) return;
        clearAnimationFilterHistory(0, m_animations[size_t(i)].id);
        m_animations.erase(m_animations.begin() + i);
    }
    emit animationsChanged();
}

// ---------------------------------------------------------------------------
// Transport

void Engine::controlAnimation(quint64 id, AnimAction action, double time, AnimLoop loop, int repeat)
{
    static const char *const actions[] = {"play", "pause", "stop", "rewind", "seek", "loop_mode", "speed"};
    journal(QStringLiteral("timeline"),
            QStringLiteral("%1 %2").arg(id).arg(QLatin1String(actions[std::clamp(int(action), 0, 6)])));
    {
        Lock lk(&m_mutex);
        Animation *a = nullptr;
        for (Animation &x : m_animations)
            if (x.id == id) a = &x;
        if (!a) return;
        controlLocked(a, action, time, loop, repeat);
    }
    if (action == AnimAction::LoopMode || action == AnimAction::Speed) emit animationsChanged(); // its definition (saved with it)
}

void Engine::controlLocked(Animation *a, AnimAction action, double time, AnimLoop loop, int repeat, Layer *owner)
{
    if (action == AnimAction::Play) resumeAfterStop();
    if (action == AnimAction::Rewind || action == AnimAction::Stop || action == AnimAction::Seek ||
        (action == AnimAction::Play && a->state == AnimState::Stopped))
        clearAnimationFilterHistory(owner ? owner->id : 0, owner ? 0 : a->id);
    auto apply = [this, a, owner] {
        if (owner) applyLayerAnim(*owner, *a);
        else applyAnimation(*a);
    };
    // The values the "current value" keys start from: read as the timeline starts
    auto capture = [this, a] {
        for (AnimTrack &t : a->tracks) {
            double v = 0;
            const bool cur = !t.oscillator && !t.keys.empty() && t.keys.front().isCurrentValue;
            t.captured = cur && !t.color && animParamValue(t.layer, t.param, &v) ? v : std::numeric_limits<double>::quiet_NaN();
            t.capturedRgb = AnimTrack().capturedRgb;
            if (cur && t.color) animColorValue(t.layer, t.param, &t.capturedRgb);
            if (t.isPoint()) for (auto &axis : t.axes) {
                const bool current = !axis.oscillator && !axis.keys.empty() && axis.keys.front().isCurrentValue;
                axis.captured = current && animParamValue(t.layer, axis.param, &v) ? v : std::numeric_limits<double>::quiet_NaN();
            }
        }
    };
    // What the action does to the transport is pxs-core's; around it, what NyxMap does: the values the "current value" keys start from,
    // the values set at once, the events at the start
    const bool fresh = a->control(action, time, loop, repeat);
    switch (action) {
    case AnimAction::Play:
        if (fresh) {
            capture();
            if (!owner) fireAnimationEvents(*a, 0, 0, true); // the events at the start
        }
        apply(); // its first values now, not a frame later
        break;
    case AnimAction::Rewind:
        capture();
        apply();
        if (!owner && a->state == AnimState::Playing) fireAnimationEvents(*a, 0, 0, true); // played again from the start
        break;
    case AnimAction::Seek:
        if (fresh) capture();
        apply();
        break;
    default:
        break;
    }
}

void Engine::clearAnimationFilterHistory(quint64 layer, quint64 animation)
{
    const QString prefix = QString::number(layer) + ":" + QString::number(animation) + ":";
    for (auto it = m_animationFilters.begin(); it != m_animationFilters.end();) {
        if (animation ? it.key().section(':', 1, 1).toULongLong() == animation : it.key().startsWith(prefix))
            it = m_animationFilters.erase(it);
        else
            ++it;
    }
}

bool Engine::animationFilterSignal(quint64 layer, quint64 animation, const QString &path, double *input,
                                   std::vector<double> *stages) const
{
    Lock lk(&m_mutex);
    const auto it = m_animationFilters.constFind(QString::number(layer) + ":" + QString::number(animation) + ":" + path);
    if (it == m_animationFilters.constEnd() || !it->processor.level()) return false;
    if (input) *input = *it->processor.level();
    if (stages) *stages = it->processor.stageOutputs();
    return true;
}

double Engine::filterAnimationValue(quint64 layer, quint64 animation, const QString &path, const AnimTrack &track,
                                    double raw, double dt, double played)
{
    if (track.dataFilters.isEmpty() || !std::isfinite(raw)) return raw;
    const QString key = QString::number(layer) + ":" + QString::number(animation) + ":" + path;
    auto &state = m_animationFilters[key];
    if (played < state.played) state.processor.reset();
    state.played = played;
    if (state.definition != track.dataFilters) {
        state.config.filters = Interaction::filtersFromJson(QJsonObject{{"filters", track.dataFilters}});
        state.definition = track.dataFilters;
    }
    state.processor.input(state.config, raw);
    return state.processor.step(state.config, dt).value_or(raw);
}

void Engine::applyAnimation(Animation &a, double dt)
{
    const double pos = a.position(), played = a.played();
    for (const AnimTrack &t : a.tracks) {
        if (!t.enabled || t.param.isEmpty() || t.event) continue;
        if (t.isPoint()) {
            setAnimPoint(t, pos, played, a.id, dt);
            continue;
        }
        if (t.color) {
            auto rgb = t.colorAt(pos);
            for (int c = 0; c < 3; ++c)
                rgb[size_t(c)] = filterAnimationValue(t.layer, a.id, t.param + QString("/rgb%1").arg(c), t,
                                                      rgb[size_t(c)], dt, played);
            setAnimColor(t.layer, t.param, rgb);
            continue;
        }
        const double v = filterAnimationValue(t.layer, a.id, t.param, t, t.valueAt(pos, played), dt, played);
        if (std::isfinite(v) && setAnimParam(t.layer, t.param, v) && t.layer) releaseFromFades(t.layer, t.param);
    }
}

void Engine::stepAnimations(double dt)
{
    dt = std::max(0.0, dt);
    // The lights and cameras going along their paths (2: an open path goes there and back)
    for (auto &lp : m_layers) {
        for (Fx3dEntry &fx : lp->fx3d) {
            if (!fx.enabled) continue;
            if (fx.kind == Fx3d::FollowPath && fx.s.layer && fx.s.speed != 0) // a 3D object's Follow Path FX
                fx.travel = std::fmod(fx.travel + double(fx.s.speed) * dt, 2.0);
            else if (fx.kind == Fx3d::Spin && fx.s.speed != 0) // a Spin turning: its turns, kept within one
                fx.travel = std::fmod(fx.travel + double(fx.s.speed) * dt, 1.0);
            else if (fx.kind == Fx3d::Noise && fx.s.rate != 0) // a Noise changing its values: its steps, as many as it has gone through
                fx.travel = std::fmod(fx.travel + double(fx.s.rate) * dt, 1048576.0);
        }
    }
    // The layers' own numbers first: a timeline playing the same number wins
    for (auto &lp : m_layers) {
        Layer &l = *lp;
        // Modulators run before the animations they control, including nested amplitudes.
        if (l.anims.empty()) continue;
        std::vector<Animation *> ordered;
        ordered.reserve(l.anims.size());
        for (auto &a : l.anims)
            ordered.push_back(&a);
        auto depth = [](const Animation *a) {
            QString path = a->tracks.empty() ? QString() : a->tracks.front().param;
            int n = 0;
            while (path.startsWith("animation/") && n < 32) {
                path = QUrl::fromPercentEncoding(path.section('/', 1, 1).toLatin1());
                ++n;
            }
            return n;
        };
        std::stable_sort(ordered.begin(), ordered.end(), [&](auto *a, auto *b) { return depth(a) > depth(b); });
        for (Animation *entry : ordered) {
            Animation &a = *entry;
            if (a.state != AnimState::Playing || a.tracks.empty() || !a.tracks.front().enabled) continue;
            const bool going = a.advance(dt);
            applyLayerAnim(l, a, dt);
            if (!going) { // over: its last values stay
                a.state = AnimState::Stopped;
                a.clock = 0;
                a.oscillatorOffset = 0;
                a.reversed = false;
            }
        }
    }
    for (Animation &a : m_animations) {
        if (a.state != AnimState::Playing) continue;
        const double from = a.clock;
        const bool going = a.advance(dt);
        applyAnimation(a, dt);
        fireAnimationEvents(a, from, a.clock, false);
        if (!going) { // over: its last values stay
            a.state = AnimState::Stopped;
            a.clock = 0;
            a.oscillatorOffset = 0;
            a.reversed = false;
        }
    }
}

void Engine::fireAnimationEvents(const Animation &a, double from, double to, bool fromIncluded)
{
    const double d = passDuration(a.duration), end = std::min(to, a.length());
    if (!(end >= from)) return;
    // The passes the clock goes through (the last one of a finite count ends at length(): no pass after it)
    const double passes = a.loop == AnimLoop::Once ? 1 : a.repeat > 0 ? a.repeat : std::numeric_limits<double>::infinity();
    const double first = std::min(std::floor(from / d), passes - 1), last = std::min(std::floor(end / d), passes - 1);
    for (const AnimTrack &t : a.tracks) {
        if (!t.event || !t.enabled || t.param.isEmpty() || t.keys.empty()) continue;
        // A flag sets what a flag can (not a number a curve drives); setEventValue leaves a locked layer alone
        const Parameter *p = findParameter(t.layer, t.param);
        if (!p || !isEventTarget(p->info())) continue;
        std::vector<double> fired(t.keys.size(), -1); // a ping-pong's turn: the same key ends one pass and starts the next
        std::vector<std::pair<double, size_t>> due;   // the flags reached in this step, in the order of the clock
        for (double pass = first; pass <= last && pass - first < 1000; ++pass) {
            const bool back = a.backwardsAt(pass * d + d / 2);
            for (size_t k = 0; k < t.keys.size(); ++k) {
                const double c = pass * d + (back ? d - t.keys[k].t : t.keys[k].t);
                if ((c > from || (fromIncluded && c >= from)) && c <= end && std::abs(c - fired[k]) > 1e-9) {
                    fired[k] = c;
                    due.push_back({c, k});
                }
            }
        }
        std::stable_sort(due.begin(), due.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
        for (const auto &[c, k] : due) {
            setEventValue(t.layer, t.param, t.keys[k].v, false);
            journal(QStringLiteral("timeline"), QStringLiteral("%1 event %2").arg(a.id).arg(t.param));
        }
    }
}

// ---------------------------------------------------------------------------
// The numbers a timeline drives

const std::vector<Parameter *> &Engine::compositionParameters()
{
    if (!m_compParamList.empty()) return m_compParamList;
    auto add = [this](const QString &path, const QString &label) -> Parameter & {
        m_compParams.push_back(std::make_unique<Parameter>(path, label, Parameter::Type::Float));
        return *m_compParams.back();
    };
    add("opacity", "Opacity").range(0, 1).byDefault(1.0).bind([this] { return QVariant(m_compositionOpacityTarget); },
                                                               [this](const QVariant &v) {
                                                                   m_compositionOpacityTarget = v.toDouble();
                                                                   m_compositionOpacitySpeed = 0;
                                                               });
    add("volume", "Volume").range(0, 2).byDefault(1.0).bind([this] { return QVariant(double(m_audio->volume())); },
                                                             [this](const QVariant &v) { m_audio->setVolume(float(v.toDouble())); });
    add("speed", "Speed").range(0, 10).byDefault(1.0).bind([this] { return QVariant(m_compositionSpeed.load()); },
                                                           [this](const QVariant &v) { setCompositionSpeed(v.toDouble()); });
    // What a frame of 3D may cost, for all the composition's 3D scenes: a setting of the project, not a value of the show
    const auto quality3d = [this](const QString &path, const QString &label, int Scene3DQuality::*m, const QStringList &levels) {
        m_compParams.push_back(std::make_unique<Parameter>(path, label, Parameter::Type::Choice));
        m_compParams.back()
            ->choices(levels)
            .byDefault(levels.value(Scene3DQuality().*m))
            .ramp(Parameter::Ramp::Cut)
            .animatable(false)
            .transient()
            .bind([this, m, levels] { return QVariant(levels.value(m_quality3d.*m)); },
                  [this, m, levels](const QVariant &v) { m_quality3d.*m = std::max(0, int(levels.indexOf(v.toString()))); });
    };
    const QStringList levels{QStringLiteral("off"), QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")};
    quality3d("quality3d/antialiasing", "3D Quality › Antialiasing", &Scene3DQuality::antialiasing,
              {QStringLiteral("off"), QStringLiteral("2x"), QStringLiteral("4x"), QStringLiteral("8x")});
    quality3d("quality3d/shadows", "3D Quality › Shadows", &Scene3DQuality::shadows, levels);
    quality3d("quality3d/contact", "3D Quality › Contact Shadows", &Scene3DQuality::contact, levels);
    quality3d("quality3d/reflections", "3D Quality › Reflections", &Scene3DQuality::reflections, levels);
    quality3d("quality3d/glow", "3D Quality › Glow", &Scene3DQuality::glow, levels);
    quality3d("quality3d/atmosphere", "3D Quality › Fog", &Scene3DQuality::atmosphere, levels);
    // Read-only sources for Internal interactions; never saved or driven by a timeline.
    add("audio/level", "Audio analysis › Level").range(0,1).readOnly().bind([this] { return QVariant(m_audio->analysis().level); }, {});
    add("audio/bass", "Audio analysis › Bass").range(0,1).readOnly().bind([this] { return QVariant(m_audio->analysis().bass); }, {});
    add("audio/mid", "Audio analysis › Mid").range(0,1).readOnly().bind([this] { return QVariant(m_audio->analysis().mid); }, {});
    add("audio/high", "Audio analysis › High").range(0,1).readOnly().bind([this] { return QVariant(m_audio->analysis().high); }, {});
    add("audio/onset", "Audio analysis › Attacks").range(0,1).readOnly().bind([this] { return QVariant(m_audio->analysis().onset); }, {});
    for (auto &p : m_compParams) m_compParamList.push_back(p.get());
    return m_compParamList;
}

const std::vector<std::unique_ptr<Parameter>> &Engine::interactionParams()
{
    if (m_interactionParamsDirty) {
        makeInteractionParams();
        m_interactionParamsDirty = false;
    }
    return m_interactionParams;
}

Parameter *Engine::findParameter(quint64 layer, const QString &path)
{
    if (!layer) {
        if (path.startsWith(QLatin1String("interaction/"))) { // an interaction's (apart from the composition's own)
            for (const auto &p : interactionParams())
                if (p->path() == path) return p.get();
            return nullptr;
        }
        if (isShowParam(path)) { // the show's actions and states
            showParams(); // made again if what they act on changed
            return m_showParamIndex.value(path);
        }
        if (path.startsWith(QLatin1String("fixture/"))) { // a fixture's of the pixel mapping
            for (const auto &p : fixtureParams())
                if (p->path() == path) return p.get();
            return nullptr;
        }
        for (Parameter *p : compositionParameters())
            if (p->path() == path) return p;
        return nullptr;
    }
    for (auto &l : m_layers)
        if (l->id == layer) return l->parameter(path);
    return nullptr;
}

bool Engine::setAnimParam(quint64 layer, const QString &path, double v)
{
    if (layer)
        for (auto &l : m_layers)
            if (l->id == layer && l->locked) return false; // a timeline leaves a locked layer alone
    Parameter *p = findParameter(layer, path);
    return p && p->isNumber() && p->setNumber(v);
}

bool Engine::animParamValue(quint64 layer, const QString &path, double *value) const
{
    Lock lk(&m_mutex);
    const Parameter *p = const_cast<Engine *>(this)->findParameter(layer, path);
    if (!p || !p->isNumber()) return false;
    *value = p->number();
    return true;
}

std::vector<ParamInfo> Engine::parameters(quint64 layer) const
{
    Lock lk(&m_mutex);
    std::vector<ParamInfo> out;
    auto *self = const_cast<Engine *>(this); // the lists are made when they are asked for
    const std::vector<Parameter *> *list = nullptr;
    if (!layer) list = &self->compositionParameters();
    for (auto &l : m_layers)
        if (l->id == layer) list = &l->parameters();
    if (list)
        for (const Parameter *p : *list) out.push_back(p->info());
    if (!layer) {
        for (const auto &p : self->showParams()) out.push_back(p->info());
        for (const auto &p : self->interactionParams())
            out.push_back(p->info());
        for (const auto &p : self->fixtureParams())
            out.push_back(p->info());
    }
    return out;
}

std::vector<Engine::AnimParam> Engine::animatableParams(quint64 layer) const
{
    Lock lock(&m_mutex);
    std::vector<AnimParam> out;
    for (const ParamInfo &i : parameters(layer))
        if (i.animatable) out.push_back(i);
    return out;
}

std::vector<Engine::AnimParam> Engine::triggerParams(quint64 layer) const
{
    std::vector<AnimParam> out;
    for (const ParamInfo &i : parameters(layer))
        if (i.type == ParamInfo::Type::Trigger && (i.access & ParamInfo::Write)) out.push_back(i);
    return out;
}

bool Engine::setEventValue(quint64 layer, const QString &param, double v, bool release)
{
    if (layer && std::any_of(m_layers.begin(), m_layers.end(), [&](const auto &l) { return l->id == layer && l->locked; }))
        return false;
    Parameter *p = findParameter(layer, param);
    if (!p) return false;
    const ParamInfo info = p->info();
    if (!(info.access & ParamInfo::Write) || info.type == ParamInfo::Type::Text) return false;
    QVariant value; // an action: none
    if (info.type == ParamInfo::Type::Bool) value = v >= 0.5;
    else if (info.type == ParamInfo::Type::Choice)
        value = info.choices.value(std::clamp(int(std::lround(v)), 0, std::max(0, int(info.choices.size()) - 1)));
    else if (info.isNumber()) value = v;
    const bool wrote = p->setValue(value);
    if (wrote && release && layer) releaseFromFades(layer, param); // a fade running on it lets go
    return wrote;
}

bool Engine::isEventTarget(const ParamInfo &i)
{
    if (!(i.access & ParamInfo::Write)) return false;
    switch (i.type) {
    case ParamInfo::Type::Trigger:
    case ParamInfo::Type::Bool:
    case ParamInfo::Type::Choice: return true;
    case ParamInfo::Type::Text: return false;
    default: return !i.animatable; // a number a curve drives is a curve's, not a flag's (Seek To, In, Elapsed…)
    }
}

std::vector<Engine::AnimParam> Engine::settableParams(quint64 layer) const
{
    std::vector<AnimParam> out;
    for (const ParamInfo &i : parameters(layer))
        if (i.type != ParamInfo::Type::Trigger && isEventTarget(i)) out.push_back(i);
    return out;
}

std::vector<Engine::AnimParam> Engine::animatableColors(quint64 layer) const
{
    const auto params = animatableParams(layer);
    std::vector<AnimParam> out;
    for (const auto &p : params) {
        if (p.colorComponent != 0 || !p.path.endsWith("/r")) continue;
        const QString path = p.path.chopped(2);
        bool complete = true;
        for (int c = 1; c < 3; ++c) {
            const QString channel = path + '/' + QChar("rgb"[c]);
            complete &= std::any_of(params.begin(), params.end(), [&](const auto &x) {
                return x.path == channel && x.colorComponent == c;
            });
        }
        if (!complete) continue;
        AnimParam target = p;
        target.path = path;
        target.label = p.label.chopped(2) + QStringLiteral(" (RGB)");
        out.push_back(target);
    }
    return out;
}

std::vector<Engine::AnimParam> Engine::animatablePoints(quint64 layer) const
{
    const auto params = animatableParams(layer);
    std::vector<AnimParam> out;
    for (const auto &p : params) {
        if (p.pointComponent != 0 || !p.path.endsWith("/x")) continue;
        const QString base = p.path.chopped(2);
        if (std::none_of(params.begin(), params.end(), [&](const auto &y) {
                return y.path == base + "/y" && y.pointComponent == 1;
            })) continue;
        AnimParam group = p;
        group.path = base;
        group.label = p.label;
        const int suffix = group.label.lastIndexOf(QStringLiteral(" X"));
        if (suffix >= 0) group.label.remove(suffix, 2);
        group.label += QStringLiteral(" (XY)");
        out.push_back(group);
    }
    return out;
}

bool Engine::setAnimPoint(const AnimTrack &track, double position, double played, quint64 animation, double dt)
{
    if (!track.isPoint()) return false;
    for (const auto &l : m_layers) if (l->id == track.layer && l->locked) return false;
    std::array<Parameter *, 2> params{};
    std::array<double, 2> values{};
    for (int c = 0; c < 2; ++c) {
        params[size_t(c)] = findParameter(track.layer, track.param + (c ? "/y" : "/x"));
        const auto &axis = track.axes[size_t(c)];
        values[size_t(c)] = axis.enabled ? filterAnimationValue(track.layer, animation, track.param + (c ? "/y" : "/x"),
                                                                axis, axis.valueAt(position, played), dt, played)
                                         : 0;
        const auto *p = params[size_t(c)];
        if (!p || !p->animatable() || p->info().pointComponent != c || !std::isfinite(values[size_t(c)])) return false;
    }
    for (int c = 0; c < 2; ++c) if (track.axes[size_t(c)].enabled) {
        params[size_t(c)]->setNumber(values[size_t(c)]);
        if (track.layer) releaseFromFades(track.layer, params[size_t(c)]->path());
    }
    return true;
}

bool Engine::animColorValue(quint64 layer, const QString &path, std::array<double, 3> *value) const
{
    if (!value) return false;
    Lock lk(&m_mutex);
    std::array<double, 3> rgb;
    for (int c = 0; c < 3; ++c) {
        const Parameter *p = const_cast<Engine *>(this)->findParameter(layer, path + '/' + QChar("rgb"[c]));
        if (!p || !p->isNumber() || p->colorComponent() != c) return false;
        rgb[c] = p->number();
    }
    *value = rgb;
    return true;
}

bool Engine::setAnimColor(quint64 layer, const QString &path, const std::array<double, 3> &value)
{
    for (const auto &l : m_layers) if (l->id == layer && l->locked) return false;
    std::array<Parameter *, 3> channels;
    for (int c = 0; c < 3; ++c) {
        channels[c] = findParameter(layer, path + '/' + QChar("rgb"[c]));
        const Parameter *p = channels[c];
        if (!std::isfinite(value[c]) || !p || !p->animatable() || p->colorComponent() != c) return false;
    }
    // Validate the entire target first: a missing/replaced shader must never receive a partial color.
    for (int c = 0; c < 3; ++c) {
        channels[c]->setNumber(value[c]);
        if (layer) releaseFromFades(layer, channels[c]->path());
    }
    return true;
}

Engine::AnimationTargetState Engine::captureAnimationTargets(quint64 layer) const
{
    Lock lk(&m_mutex);
    AnimationTargetState state;
    state.layer = layer;
    for (const auto &l : m_layers) if (l->id == layer) state.layerAnimations = l->anims;
    for (const auto &a : m_animations)
        if (std::any_of(a.tracks.begin(), a.tracks.end(), [layer](const AnimTrack &t) { return t.layer == layer; }))
            state.timelines.push_back(a);
    return state;
}

void Engine::restoreAnimationTargets(const AnimationTargetState &state)
{
    bool changed = false;
    {
        Lock lk(&m_mutex);
        if (!state.layer || indexOfId(state.layer) < 0) return;
        restoreLayerAnims(state.layer, state.layerAnimations);
        for (const auto &before : state.timelines) {
            const int index = indexOfAnimation(before.id);
            if (index < 0) continue;
            // Restore the captured start values too. setAnimation() intentionally discards captures for
            // newly added tracks, which would change an undone "start from current value" transition.
            m_animations[size_t(index)].tracks = before.tracks;
            changed = true; // the shared timeline's clock and transport keep running
        }
    }
    if (changed) emit animationsChanged();
}

void Engine::remapAnimationTargets(quint64 layer, const std::vector<std::pair<QString, QString>> &prefixes)
{
    if (prefixes.empty()) return;
    Lock lk(&m_mutex);
    bool localChanged = false, sharedChanged = false;
    auto remap = [&](AnimTrack &track) {
        if (track.layer != layer) return false;
        for (const auto &[from, to] : prefixes) {
            const bool match = track.param == from || track.param.startsWith(from + '/') ||
                               ((track.color || track.isPoint()) && from.startsWith(track.param + '/'));
            if (!match) continue;
            if (to.isEmpty()) return true;
            track.param = to + track.param.mid(from.size());
            sortAnimKeys(track);
            return false;
        }
        return false;
    };
    for (auto &l : m_layers) if (l->id == layer) {
        for (auto it = l->anims.begin(); it != l->anims.end();) {
            if (!it->tracks.empty()) {
                const QString before = it->tracks.front().param;
                if (remap(it->tracks.front())) { it = l->anims.erase(it); localChanged = true; continue; }
                localChanged |= it->tracks.front().param != before;
            }
            ++it;
        }
    }
    for (auto &a : m_animations) {
        for (auto it = a.tracks.begin(); it != a.tracks.end();) {
            const QString before = it->param;
            if (remap(*it)) { it = a.tracks.erase(it); sharedChanged = true; continue; }
            sharedChanged |= it->param != before;
            ++it;
        }
    }
    // Source/FX edits call this under the lock. Refresh editors after the new topology is complete.
    if (localChanged || sharedChanged) QMetaObject::invokeMethod(this, [this, layer, localChanged, sharedChanged] {
        if (localChanged) emit layerAnimsChanged(layer);
        if (sharedChanged) emit animationsChanged();
    }, Qt::QueuedConnection);
}

void Engine::removeSourceAnimations(Layer &layer)
{
    std::vector<std::pair<QString, QString>> removed;
    for (const auto *parameter : layer.parameters())
        if (partOf(parameter->path()) & (PartSource | PartTransport)) removed.push_back({parameter->path(), {}});
    remapAnimationTargets(layer.id, removed);
}

bool Engine::parameterInfo(quint64 layer, const QString &path, ParamInfo *info) const
{
    Lock lk(&m_mutex);
    const Parameter *p = const_cast<Engine *>(this)->findParameter(layer, path);
    if (!p) return false;
    if (info) *info = p->info();
    return true;
}

QJsonValue Engine::parameterValue(quint64 layer, const QString &path) const
{
    Lock lk(&m_mutex);
    const Parameter *p = const_cast<Engine *>(this)->findParameter(layer, path);
    return p && p->isStored() ? p->json() : QJsonValue(QJsonValue::Undefined);
}

bool Engine::setParameter(quint64 layer, const QString &path, const QJsonValue &v)
{
    Lock lk(&m_mutex);
    Parameter *p = findParameter(layer, path);
    if (!p || !p->setJson(v)) return false;
    if (layer) releaseFromFades(layer, path);
    return true;
}

QString Engine::shaderPrefix(quint64 layer, int slot) const
{
    if (slot < 0) return QString();
    Lock lk(&m_mutex);
    for (const auto &l : m_layers) {
        if (l->id != layer) continue;
        QStringList names;
        for (const auto &x : l->effects) names << x->name();
        const QStringList segs = osc::uniqueSegments(names);
        return slot < segs.size() ? QStringLiteral("fx/%1/").arg(segs[slot]) : QString();
    }
    return QString();
}

// ---------------------------------------------------------------------------
// The animations of a layer's numbers

void Engine::applyLayerAnim(Layer &l, Animation &a, double dt)
{
    if (a.tracks.empty()) return;
    const AnimTrack &t = a.tracks.front();
    if (!t.enabled || t.param.isEmpty()) return;
    if (t.isPoint()) {
        setAnimPoint(t, a.position(), a.played(), 0, dt);
        return;
    }
    if (t.color) {
        auto rgb = t.colorAt(a.position());
        for (int c = 0; c < 3; ++c)
            rgb[size_t(c)] =
                filterAnimationValue(l.id, 0, t.param + QString("/rgb%1").arg(c), t, rgb[size_t(c)], dt, a.played());
        setAnimColor(l.id, t.param, rgb);
        return;
    }
    const double v = filterAnimationValue(l.id, 0, t.param, t, t.valueAt(a.position(), a.played()), dt, a.played());
    Parameter *p = l.parameter(t.param);
    if (std::isfinite(v) && p && p->setNumber(v)) releaseFromFades(l.id, t.param);
}

void Engine::startLayerAnim(Animation &a)
{
    a.state = AnimState::Stopped; // Play from the start, its "current value" keys read now
    Layer *owner = nullptr;
    for (auto &lp : m_layers)
        for (Animation &x : lp->anims)
            if (&x == &a) owner = lp.get();
    controlLocked(&a, AnimAction::Play, 0, a.loop, a.repeat, owner);
}

static Animation *findLayerAnim(Layer &l, const QString &param)
{
    for (Animation &a : l.anims)
        if (!a.tracks.empty() && a.tracks.front().param == param) return &a;
    return nullptr;
}

std::vector<Animation> Engine::layerAnims(quint64 layer) const
{
    Lock lk(&m_mutex);
    for (auto &l : m_layers)
        if (l->id == layer) return l->anims;
    return {};
}

bool Engine::layerAnim(quint64 layer, const QString &param, Animation *a) const
{
    Lock lk(&m_mutex);
    for (auto &l : m_layers)
        if (l->id == layer)
            if (const Animation *x = findLayerAnim(*l, param)) {
                if (a) *a = *x;
                return true;
            }
    return false;
}

void Engine::assignLayerAnims(Layer &l, const std::vector<Animation> &anims)
{
    clearAnimationFilterHistory(l.id, 0);
    std::vector<Animation> old = std::move(l.anims);
    l.anims.clear();
    for (const Animation &in : anims) {
        if (in.tracks.empty() || in.tracks.front().param.isEmpty() || findLayerAnim(l, in.tracks.front().param))
            continue; // one animation per number
        Animation a = in;
        a.tracks.resize(1);
        AnimTrack &t = a.tracks.front();
        t.layer = l.id;
        sortAnimKeys(t);
        a.state = AnimState::Stopped;
        a.clock = 0;
        a.oscillatorOffset = 0;
        a.reversed = false;
        const Animation *was = nullptr;
        for (const Animation &o : old)
            if (!o.tracks.empty() && o.tracks.front().param == t.param) was = &o;
        if (was) { // how its card is shown is not part of an edit
            a.pinned = was->pinned;
            a.folded = was->folded;
        }
        if (was && was->tracks.front().enabled && t.enabled) { // it goes on from where it is
            a.state = was->state;
            a.clock = was->clock;
            a.oscillatorOffset = was->oscillatorOffset;
            a.reversed = was->reversed;
            a.loop = was->loop;
            a.repeat = was->repeat;
            a.setLoop(in.loop, in.repeat);
            a.clock = std::min(a.clock, a.length());
            t.captured = was->tracks.front().captured;
            t.capturedRgb = was->tracks.front().capturedRgb;
            if (t.isPoint() && was->tracks.front().isPoint()) for (int c = 0; c < 2; ++c)
                t.axes[size_t(c)].captured = was->tracks.front().axes[size_t(c)].captured;
        }
        l.anims.push_back(a);
    }
    // New, or turned on: from the start
    for (Animation &a : l.anims) {
        const QString &param = a.tracks.front().param;
        bool wasOn = false;
        for (const Animation &o : old)
            if (!o.tracks.empty() && o.tracks.front().param == param) wasOn = o.tracks.front().enabled;
        if (a.tracks.front().enabled && !wasOn) controlLocked(&a, AnimAction::Play, 0, a.loop, a.repeat, &l);
    }
}

void Engine::restoreLayerAnims(quint64 layer, std::vector<Animation> anims)
{
    {
        Lock lock(&m_mutex);
        const int index = indexOfId(layer);
        if (index < 0 || isLocked(index)) return;
        auto &current = m_layers[size_t(index)]->anims;
        for (Animation &restored : anims) {
            if (restored.tracks.empty()) continue;
            const auto shown = std::find_if(current.begin(), current.end(), [&](const Animation &a) {
                return !a.tracks.empty() && a.tracks.front().param == restored.tracks.front().param;
            });
            if (shown != current.end()) {
                restored.pinned = shown->pinned;
                restored.folded = shown->folded;
            }
        }
        clearAnimationFilterHistory(layer, 0);
        current = std::move(anims);
    }
    emit layerAnimsChanged(layer);
}

void Engine::setLayerAnims(quint64 layer, const std::vector<Animation> &anims)
{
    {
        Lock lk(&m_mutex);
        Layer *l = nullptr;
        for (auto &x : m_layers)
            if (x->id == layer) l = x.get();
        if (!l) return;
        assignLayerAnims(*l, anims);
    }
    emit layerAnimsChanged(layer);
}

void Engine::setLayerAnim(quint64 layer, const QString &param, const Animation *a, int at)
{
    {
        Lock lk(&m_mutex);
        Layer *l = nullptr;
        for (auto &x : m_layers)
            if (x->id == layer) l = x.get();
        if (!l) return;
        std::vector<Animation> list = l->anims; // the others as they are
        auto it = std::find_if(list.begin(), list.end(), [&](const Animation &x) { return x.tracks.front().param == param; });
        if (a && it != list.end()) {
            *it = *a;
        } else if (a) {
            list.insert(at >= 0 && at <= int(list.size()) ? list.begin() + at : list.end(), *a);
        } else if (it != list.end()) {
            list.erase(it);
        } else {
            return;
        }
        assignLayerAnims(*l, list);
    }
    emit layerAnimsChanged(layer);
}

void Engine::setLayerAnimOn(quint64 layer, const QString &param, bool on)
{
    {
        Lock lk(&m_mutex); // read and written under the same lock: an edit meanwhile is not lost
        Layer *l = nullptr;
        for (auto &x : m_layers)
            if (x->id == layer) l = x.get();
        Animation *a = l ? findLayerAnim(*l, param) : nullptr;
        if (!a || a->tracks.front().enabled == on) return;
        std::vector<Animation> list = l->anims;
        for (Animation &x : list)
            if (x.tracks.front().param == param) x.tracks.front().enabled = on;
        assignLayerAnims(*l, list);
    }
    emit layerAnimsChanged(layer);
}

int Engine::layerAnimIndex(quint64 layer, const QString &param) const
{
    Lock lk(&m_mutex);
    for (auto &l : m_layers)
        if (l->id == layer)
            for (size_t i = 0; i < l->anims.size(); ++i)
                if (l->anims[i].tracks.front().param == param) return int(i);
    return -1;
}

void Engine::setLayerAnimView(quint64 layer, const QString &param, bool pinned, bool folded)
{
    {
        Lock lk(&m_mutex);
        for (auto &l : m_layers)
            if (l->id == layer)
                if (Animation *a = findLayerAnim(*l, param)) {
                    a->pinned = pinned;
                    a->folded = folded;
                }
    }
    emit layerAnimsChanged(layer);
}

void Engine::controlLayerAnim(quint64 layer, const QString &param, AnimAction action, double time, AnimLoop loop, int repeat)
{
    {
        Lock lk(&m_mutex);
        for (auto &l : m_layers)
            if (l->id == layer)
                if (Animation *a = findLayerAnim(*l, param)) controlLocked(a, action, time, loop, repeat, l.get());
    }
    if (action == AnimAction::LoopMode || action == AnimAction::Speed) emit layerAnimsChanged(layer); // its definition
}

void Engine::waveAround(quint64 layer, const QString &param, double *center, double *amplitude) const
{
    double lo = 0, hi = 1, v = 0;
    for (const AnimParam &p : animatableParams(layer))
        if (p.path == param) lo = p.min, hi = p.max;
    if (!animParamValue(layer, param, &v)) v = (lo + hi) / 2;
    QSize comp;
    {
        Lock lk(&m_mutex);
        comp = m_compSize;
    }
    const bool horizontal = param.endsWith("/x") || param.endsWith("/width");
    *center = v;
    ParamInfo info;
    parameterInfo(layer, param, &info);
    if (info.ramp == ParamInfo::Ramp::Angle) {
        *center = 0; // a saw turns round and round, a sine swings
        *amplitude = 180;
    } else if (param.startsWith("position/") || param.startsWith("pivot/") || param == "width" || param == "height") {
        *amplitude = 0.1 * (horizontal ? comp.width() : comp.height()); // pixels: a tenth of the composition
    } else if (param.startsWith("scale/")) {
        *amplitude = 25;
    } else {
        *amplitude = std::min(v - lo, hi - v); // as far as it can go both ways…
        if (*amplitude < 0.1 * (hi - lo) / 2) { // …or, at a bound, the whole range
            *center = (lo + hi) / 2;
            *amplitude = (hi - lo) / 2;
        }
    }
}

Animation Engine::makeLayerAnim(quint64 layer, const QString &param, int wave) const
{
    const auto points = animatablePoints(layer);
    if (std::any_of(points.begin(), points.end(), [&](const auto &p) { return p.path == param; })) {
        Animation a;
        AnimTrack t;
        t.layer = layer;
        t.param = param;
        for (int c = 0; c < 2; ++c) {
            auto axis = makeLayerAnim(layer, param + (c ? "/y" : "/x"), wave).tracks.front();
            if (wave >= 0 && c == 1) axis.phase = 0.25; // a quarter cycle: an ellipse for sine waves
            t.axes.push_back(axis);
        }
        a.tracks = {t};
        return a;
    }
    const auto colors = animatableColors(layer);
    if (std::any_of(colors.begin(), colors.end(), [&](const auto &p) { return p.path == param; })) {
        Animation a;
        AnimTrack t;
        t.layer = layer;
        t.param = param;
        t.color = true;
        AnimKey start{0, 0, 0}, end{a.duration, 0, 0};
        animColorValue(layer, param, &start.rgb);
        end.rgb = start.rgb;
        t.keys = {start, end};
        a.tracks = {t};
        return a;
    }
    double v = 0, center = 0, amp = 0;
    waveAround(layer, param, &center, &amp);
    if (!animParamValue(layer, param, &v)) v = center;
    Animation a;
    a.duration = 4;
    a.loop = AnimLoop::Loop;
    AnimTrack t;
    t.layer = layer;
    t.param = param;
    t.seed = QRandomGenerator::global()->generate() | 1u;
    if (wave >= 0) {
        t.oscillator = true;
        t.wave = AnimWave(std::clamp(wave, 0, kAnimWaveCount - 1));
        t.period = 2;
        t.center = center;
        t.amplitude = amp;
    } else { // from where it is, out and back
        const double far = std::abs(center + amp - v) >= std::abs(center - amp - v) ? center + amp : center - amp;
        t.keys = {AnimKey{0, v, 3}, AnimKey{a.duration / 2, far, 3}, AnimKey{a.duration, v, 3}};
    }
    a.tracks.push_back(t);
    return a;
}

// ---------------------------------------------------------------------------
// Project

// How a timeline is saved (its keys, its oscillator, its transport) is pxs-qt's (Anim.h): what a track drives is said here, beside it

QJsonObject layerAnimToJson(const Animation &a)
{
    QJsonObject o;
    if (a.tracks.empty()) return o;
    o["param"] = a.tracks.front().param;
    animTransportToJson(a, o);
    animTrackToJson(a.tracks.front(), o);
    if (a.pinned) o["pinned"] = true;
    if (a.folded) o["folded"] = true;
    // Its transport (what a snapshot recalls; a project opened plays the ones that are on)
    o["state"] = a.state == AnimState::Playing ? "playing" : a.state == AnimState::Paused ? "paused" : "stopped";
    if (a.state == AnimState::Paused) o["clock"] = a.clock;
    return o;
}

AnimState animStateFromJson(const QJsonObject &o)
{
    const QString s = o.value("state").toString();
    if (s == QLatin1String("paused")) return AnimState::Paused;
    if (s == QLatin1String("stopped")) return AnimState::Stopped;
    return AnimState::Playing; // "playing"
}

Animation layerAnimFromJson(const QJsonObject &o, quint64 layer)
{
    Animation a;
    animTransportFromJson(a, o);
    a.pinned = o.value("pinned").toBool(false);
    a.folded = o.value("folded").toBool(false);
    AnimTrack t;
    t.layer = layer;
    t.param = o.value("param").toString();
    animTrackFromJson(t, o, a.duration);
    a.tracks.push_back(t);
    return a;
}

QJsonArray Engine::animationsToJson() const
{
    QJsonArray out;
    for (const Animation &a : m_animations) {
        QJsonArray tracks;
        for (const AnimTrack &t : a.tracks) {
            QJsonObject o{{"layer", QString::number(t.layer)}, {"param", t.param}};
            animTrackToJson(t, o);
            tracks.append(o);
        }
        QJsonObject o{{"id", QString::number(a.id)}, {"name", a.name}, {"tracks", tracks}};
        animTransportToJson(a, o);
        out.append(o);
    }
    return out;
}

void Engine::animationsFromJson(const QJsonArray &arr)
{
    m_animations.clear();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Animation a;
        a.id = o.value("id").toString().toULongLong();
        a.name = o.value("name").toString();
        animTransportFromJson(a, o);
        for (const QJsonValue &tv : o.value("tracks").toArray()) {
            const QJsonObject to = tv.toObject();
            AnimTrack t;
            t.layer = to.value("layer").toString().toULongLong();
            t.param = to.value("param").toString();
            animTrackFromJson(t, to, a.duration);
            a.tracks.push_back(t);
        }
        bool taken = !a.id;
        for (const Animation &x : m_animations) taken = taken || x.id == a.id;
        if (taken) a.id = 0;
        m_animations.push_back(a);
    }
    m_nextAnimationId = 1;
    for (const Animation &a : m_animations) m_nextAnimationId = std::max(m_nextAnimationId, a.id + 1);
    for (Animation &a : m_animations)
        if (!a.id) a.id = m_nextAnimationId++;
}

// What a timeline told to play will play, in real seconds (a sequence's step waits for it)
double Engine::animationPlayTime(quint64 id) const
{
    Lock lk(&m_mutex);
    for (const Animation &a : m_animations) {
        if (a.id != id) continue;
        const double len = a.length();
        if (!std::isfinite(len)) return 0; // endless: nothing to wait for
        const double v = a.speed * timeScale();
        if (v <= 1e-9) return std::numeric_limits<double>::infinity(); // frozen: waits for its speed
        return (a.state == AnimState::Stopped ? len : std::max(0.0, len - a.clock)) / v; // in real seconds
    }
    return 0;
}

// What a timeline that was told to play still has to play, in real seconds: 0 once it is over (or stopped,
// or gone, or endless: nothing to wait for), infinity while it does not move (paused, or speed 0)
double Engine::animationRemaining(quint64 id) const
{
    Lock lk(&m_mutex);
    for (const Animation &a : m_animations) {
        if (a.id != id) continue;
        const double len = a.length();
        if (a.state == AnimState::Stopped || !std::isfinite(len)) return 0;
        const double v = a.speed * timeScale();
        if (a.state == AnimState::Paused || v <= 1e-9) return std::numeric_limits<double>::infinity();
        return std::max(0.0, len - a.clock) / v;
    }
    return 0;
}


namespace {
Engine::AnimationPlayback playbackOf(const Animation &a, bool captures)
{
    Engine::AnimationPlayback out;
    out.id = a.id; out.name = a.name; out.state = a.state; out.loop = a.loop;
    out.position = a.position(); out.duration = a.duration; out.clock = a.clock;
    out.played = a.played(); out.backwards = a.backwardsAt(a.clock);
    out.speed = a.speed; out.length = a.length(); out.tracks = int(a.tracks.size());
    out.pass = a.pass();
    if (captures) {
        out.captured.reserve(a.tracks.size());
        out.capturedRgb.reserve(a.tracks.size());
        for (const auto &track : a.tracks) {
            out.captured.push_back(track.captured);
            out.capturedRgb.push_back(track.capturedRgb);
            const double nan = std::numeric_limits<double>::quiet_NaN();
            out.capturedXy.push_back(track.isPoint() ? std::array<double, 2>{track.axes[0].captured, track.axes[1].captured}
                                                   : std::array<double, 2>{nan, nan});
        }
    }
    return out;
}
}

bool Engine::sampleAnimations(std::vector<AnimationPlayback> *out) const
{
    if (!out || !m_mutex.tryLock()) return false;
    std::vector<AnimationPlayback> sample;
    sample.reserve(m_animations.size());
    for (const auto &animation : m_animations) sample.push_back(playbackOf(animation, false));
    m_mutex.unlock();
    *out = std::move(sample);
    return true;
}

bool Engine::sampleAnimation(quint64 timeline, quint64 layer, const QString &param, AnimationPlayback *out) const
{
    if (!out || !m_mutex.tryLock()) return false;
    const Animation *found = nullptr;
    if (timeline) {
        for (const auto &animation : m_animations) if (animation.id == timeline) { found = &animation; break; }
    } else {
        for (const auto &owner : m_layers) if (owner->id == layer)
            for (const auto &animation : owner->anims)
                if (!animation.tracks.empty() && animation.tracks.front().param == param) { found = &animation; break; }
    }
    if (found) *out = playbackOf(*found, true);
    m_mutex.unlock();
    return found != nullptr;
}
