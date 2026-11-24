#include "SequenceFeedback.h"
#include "Engine.h"

#include <QHostInfo>
#include <QUdpSocket>
#include <cmath>

SequenceFeedback::SequenceFeedback(Engine *engine, QObject *parent) : QObject(parent), m_e(engine), m_namespace(engine)
{
    m_socket = new QUdpSocket(this);
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &SequenceFeedback::tick);
    // At each cue and each edit, at once (queued: the cue clock may say so from its own thread)
    connect(&m_e->sequencer(), &Sequencer::positionChanged, this, [this] { sendNow(false); }, Qt::QueuedConnection);
    connect(&m_e->sequencer(), &Sequencer::changed, this, [this] { sendNow(false); }, Qt::QueuedConnection);
    connect(m_e, &Engine::snapshotsChanged, this, [this] { sendNow(false); });
    connect(m_e, &Engine::presetsChanged, this, [this] { sendNow(false); });
    connect(m_e, &Engine::animationsChanged, this, [this] { sendNow(false); });
    // The structure (layers, controls, interactions…) changed: the addresses are not the same, the namespace is read again
    for (auto signal : {&Engine::layersChanged, &Engine::controlsChanged, &Engine::interactionsChanged, &Engine::timecodeRoutingChanged})
        connect(m_e, signal, this, [this] { sendNow(false); });
    connect(m_e, &Engine::layerAnimsChanged, this, [this] { sendNow(false); });
}

SequenceFeedback::~SequenceFeedback() { ChangeFeed::ignore(this); }

bool SequenceFeedback::parseTarget(const QString &target, QString *host, quint16 *port)
{
    const QString t = target.trimmed();
    const int colon = t.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0) return false;
    bool ok = false;
    const int p = t.mid(colon + 1).toInt(&ok);
    if (!ok || p < 1 || p > 65535) return false;
    QString h = t.left(colon).trimmed();
    if (h.startsWith(QLatin1Char('[')) && h.endsWith(QLatin1Char(']'))) h = h.mid(1, h.size() - 2); // [IPv6]:port
    if (h.isEmpty()) return false;
    if (host) *host = h;
    if (port) *port = quint16(p);
    return true;
}

void SequenceFeedback::setTargets(const QStringList &targets)
{
    auto previous = std::move(m_targets);
    m_targets.clear();
    for (const QString &t : targets) {
        Target target;
        if (!parseTarget(t, &target.host, &target.port)) continue;
        const auto existing = std::find_if(previous.begin(), previous.end(), [&](const Target &old) {
            return old.host == target.host && old.port == target.port;
        });
        if (existing != previous.end() && !existing->address.isNull()) {
            m_targets.push_back(*existing);
            continue;
        }
        if (target.host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
            target.address = QHostAddress(QHostAddress::LocalHost);
        else
            target.address = QHostAddress(target.host);
        m_targets.push_back(target);
        if (target.address.isNull()) {
            // A name: resolved in the background, sent to once it is known
            const QString host = target.host;
            QHostInfo::lookupHost(host, this, [this, host](const QHostInfo &info) {
                for (Target &t : m_targets)
                    if (t.host == host && !info.addresses().isEmpty()) t.address = info.addresses().constFirst();
            });
        }
    }
}

void SequenceFeedback::setEnabled(bool on)
{
    if (on == m_enabled) return;
    m_enabled = on;
    if (on) {
        ChangeFeed::observe(this);
        sendNow(false);
        updateTimer();
    } else {
        ChangeFeed::ignore(this);
        m_timer.stop();
        {
            std::lock_guard<std::mutex> lk(m_pendingMutex);
            m_pendingParams.clear();
            m_pendingLayers.clear();
            m_pendingAddresses.clear();
        }
        m_sent.clear();
        m_lastValues.clear();
    }
}

void SequenceFeedback::setModified(bool on)
{
    if (on == m_modified) return;
    m_modified = on;
    sendNow(false);
}

void SequenceFeedback::updateTimer()
{
    const bool running = m_enabled && [&] {
        Engine::Lock lock(&m_e->mutex());
        // A step on its way, or one played: its time since GO goes on (the stopwatch of the sequence)
        return m_e->sequencer().running() || m_e->sequencer().position() >= 0;
    }();
    if (running && !m_timer.isActive()) m_timer.start();
    else if (!running && m_timer.isActive()) m_timer.stop();
}

void SequenceFeedback::tick()
{
    if (!m_enabled || m_targets.empty()) return;
    int count = 0;
    for (const auto &message : state()) transmit(message, false, false, &count); // the times of the step on its way
    if (count) emit sent(count);
    updateTimer();
}

static QString clockText(double t)
{
    if (!std::isfinite(t)) return QStringLiteral("∞");
    t = std::max(0.0, t);
    if (t < 60) return QString::number(t, 'f', 1);
    const int m = int(t / 60);
    return QStringLiteral("%1:%2").arg(m).arg(t - m * 60, 4, 'f', 1, QLatin1Char('0'));
}

static QString continueWord(Sequencer::StepContinue c)
{
    switch (c) {
    case Sequencer::StepContinue::AutoFollow: return QStringLiteral("auto");
    default: return QStringLiteral("wait");
    }
}

static QString actionWord(const Sequencer::Step &st)
{
    using A = Engine::AnimAction;
    switch (st.action) {
    case A::Play: return QStringLiteral("Play");
    case A::Pause: return QStringLiteral("Pause");
    case A::Stop: return QStringLiteral("Stop");
    case A::Rewind: return QStringLiteral("Rewind");
    case A::Seek: return QStringLiteral("Seek %1 s").arg(st.seekTime, 0, 'f', 2);
    case A::Speed: return QStringLiteral("Speed × %1").arg(st.speed, 0, 'f', 2);
    case A::LoopMode: return QStringLiteral("Loop mode");
    }
    return {};
}

std::vector<osc::Message> SequenceFeedback::state() const
{
    return stateFor(m_e, m_modified);
}

std::vector<osc::Message> SequenceFeedback::stateFor(Engine *engine, bool modified)
{
    Engine::Lock lock(&engine->mutex());
    const Sequencer &sq = engine->sequencer();
    const int cur = sq.current();
    const Sequencer::Sequence s = sq.sequence(cur);
    const int n = int(s.steps.size());
    const std::vector<Sequencer::Run> runs = sq.runs();
    std::vector<osc::Message> out;
    auto i = [&](const QString &a, int v) { out.push_back({a, QStringLiteral("i"), {v}}); };
    auto f = [&](const QString &a, double v) { out.push_back({a, QStringLiteral("f"), {v}}); };
    auto str = [&](const QString &a, const QString &v) { out.push_back({a, QStringLiteral("s"), {v}}); };

    str(QStringLiteral("/sequence/name"), cur >= 0 ? s.name : QString());
    i(QStringLiteral("/sequence/index"), cur + 1);
    i(QStringLiteral("/sequence/steps"), n);
    i(QStringLiteral("/sequence/running"), sq.running() ? 1 : 0);
    i(QStringLiteral("/sequence/go_enabled"), sq.next() >= 0 ? 1 : 0);
    i(QStringLiteral("/sequence/back_enabled"), sq.previous() >= 0 ? 1 : 0);
    i(QStringLiteral("/sequence/modified"), modified ? 1 : 0);

    const int pos = sq.position();
    auto cue = [&](const QString &role, int k) {
        const QString base = QStringLiteral("/sequence/cue/") + role;
        const bool valid = k >= 0 && k < n;
        Sequencer::Step st;
        if (valid) st = s.steps[size_t(k)];
        QString name;
        if (valid && !st.actions.empty()) {
            name = QStringLiteral("Group · %1 actions").arg(st.actions.size());
        } else if (valid && st.sequence) {
            const int index = sq.indexOfSequence(st.sequence);
            name = index >= 0 ? QStringLiteral("Sequence · %1").arg(sq.sequence(index).name) : QStringLiteral("(sequence gone)");
        } else if (valid && st.preset) {
            const int index = engine->indexOfPreset(st.preset);
            name = index >= 0 ? QStringLiteral("Preset · %1").arg(engine->preset(index).name) : QStringLiteral("(preset gone)");
        } else if (valid && st.timeline) {
            const int a = engine->indexOfAnimation(st.timeline);
            name = (a >= 0 ? engine->animation(a).name : QStringLiteral("(timeline gone)")) + QStringLiteral(" · ") + actionWord(st);
        } else if (valid && st.snapshot) {
            const int m = engine->indexOfSnapshot(st.snapshot);
            name = m >= 0 ? engine->snapshot(m).name : QStringLiteral("(snapshot gone)");
        }
        const double duration = valid ? sq.stepDuration(st, s.beats ? engine->beatClock().secondsPerBeat() : 1.0) : 0.0;
        i(base + QStringLiteral("/number"), valid ? k + 1 : 0);
        str(base + QStringLiteral("/name"), name);
        str(base + QStringLiteral("/label"), valid ? QStringLiteral("%1 · %2").arg(k + 1).arg(name) : QString());
        str(base + QStringLiteral("/description"), st.description);
        f(base + QStringLiteral("/prewait"), st.preWait);
        f(base + QStringLiteral("/action"), std::isfinite(duration) ? duration : -1.0);
        f(base + QStringLiteral("/postwait"), st.postWait);
        str(base + QStringLiteral("/continue"), valid ? continueWord(st.next) : QString());

        // Where its time is: its run (the latest, should it be there twice in a loop)
        const Sequencer::Run *run = nullptr;
        for (const Sequencer::Run &r : runs)
            if (valid && r.step == k) run = &r;
        QString phase;
        double elapsed = 0, end = 0, phaseProgress = 0;
        auto frac = [](double e, double len) { return len > 0 ? std::clamp(e / len, 0.0, 1.0) : 1.0; };
        if (run) {
            elapsed = run->elapsed;
            end = run->end();
            const double at = run->continueAt();
            const double postStart = at - run->postWait;
            if (!run->fired) {
                phase = QStringLiteral("pre-wait");
                phaseProgress = frac(elapsed, run->preWait);
            } else if (at >= 0 && !run->continued && elapsed >= postStart && elapsed < at) {
                phase = QStringLiteral("post-wait");
                phaseProgress = frac(elapsed - postStart, run->postWait);
            } else if (elapsed - run->preWait < run->duration) {
                phase = QStringLiteral("action");
                phaseProgress = frac(elapsed - run->preWait, run->duration);
            } else {
                phase = QStringLiteral("done");
                phaseProgress = 1;
            }
        } else if (valid && k == pos) {
            // Played and over: its time since GO goes on
            phase = QStringLiteral("done");
            elapsed = sq.stepElapsed(cur);
            end = elapsed;
            phaseProgress = 1;
        }
        const double remaining = std::isfinite(end) ? std::max(0.0, end - elapsed) : -1.0;
        const double progress = phase.isEmpty() ? 0.0 : std::isfinite(end) ? frac(elapsed, end) : 0.0;
        str(base + QStringLiteral("/phase"), phase);
        f(base + QStringLiteral("/elapsed"), elapsed);
        str(base + QStringLiteral("/elapsed_text"), phase.isEmpty() ? QString() : clockText(elapsed));
        f(base + QStringLiteral("/remaining"), remaining);
        str(base + QStringLiteral("/remaining_text"), phase.isEmpty() ? QString() : clockText(remaining < 0 ? INFINITY : remaining));
        f(base + QStringLiteral("/progress"), progress);
        i(base + QStringLiteral("/percent"), int(std::lround(progress * 100)));
        f(base + QStringLiteral("/phase_progress"), phaseProgress);
        i(base + QStringLiteral("/phase_percent"), int(std::lround(phaseProgress * 100)));
    };
    cue(QStringLiteral("previous"), sq.previous());
    cue(QStringLiteral("current"), pos);
    cue(QStringLiteral("next"), sq.next());
    return out;
}

void SequenceFeedback::transmit(const osc::Message &message, bool known, bool everything, int *count)
{
    const auto packet = osc::encode(message);
    if (!known) { // the sequence's values: sent when they differ from what was sent
        const auto previous = m_sent.constFind(message.address);
        const bool unchanged = previous == m_sent.cend() || previous.value() == packet; // a first sight is a baseline
        m_sent.insert(message.address, packet);
        if (!everything && unchanged) return;
    }
    for (const auto &target : m_targets)
        if (!target.address.isNull()) m_socket->writeDatagram(packet, target.address, target.port);
    ++*count;
}

namespace {
// What a node says now, as a message when it differs from what was last read (`everything`: always); a first reading is the
// baseline, not a change
bool readNode(QHash<QString, QVariantList> &last, bool everything, const QString &path, const OscNode &node, osc::Message *out)
{
    if (node.type.empty() || !node.get || !(node.access & 1)) return false; // not something to read
    QVariantList args = pxs::convert::toVariants(node.get());
    if (args.isEmpty()) return false;
    auto it = last.find(path);
    if (!everything && it != last.end() && it.value() == args) return false;
    if (it == last.end()) {
        last.insert(path, args);
        if (!everything) return false;
    } else {
        it.value() = args;
    }
    *out = osc::Message{path, pxs::convert::fromUtf8(node.type), std::move(args)};
    for (qsizetype i = 0; i < out->types.size(); ++i)
        if (out->types[i] == 'T' || out->types[i] == 'F')
            out->types[i] = out->args.value(i).toBool() ? QLatin1Char('T') : QLatin1Char('F');
    return true;
}
} // namespace

void SequenceFeedback::sendNow(bool everything)
{
    if (!m_enabled || m_targets.empty()) return;
    auto messages = state();
    m_sequencePaths.clear();
    for (const auto &message : messages) m_sequencePaths.insert(message.address);
    std::vector<osc::Message> changed;
    {
        Engine::Lock lock(&m_e->mutex());
        m_namespace.refresh();
        if (m_namespace.generation() != m_namespaceGeneration) { // the structure changed: what is gone is forgotten
            m_namespaceGeneration = m_namespace.generation();
            QSet<QString> present;
            m_namespace.forEachReadable([&](const QString &path, const OscNode &) { present.insert(path); });
            for (auto it = m_lastValues.begin(); it != m_lastValues.end();)
                it = present.contains(it.key()) ? std::next(it) : m_lastValues.erase(it);
            for (auto it = m_sent.begin(); it != m_sent.end();)
                it = (present.contains(it.key()) || m_sequencePaths.contains(it.key())) ? std::next(it) : m_sent.erase(it);
        }
        m_namespace.forEachReadable([&](const QString &path, const OscNode &node) {
            if (m_sequencePaths.contains(path)) return;
            osc::Message message;
            if (readNode(m_lastValues, everything, path, node, &message)) changed.push_back(std::move(message));
        });
    }
    int count = 0;
    for (const auto &message : messages) transmit(message, false, everything, &count);
    for (const auto &message : changed) transmit(message, true, everything, &count);
    if (count) emit sent(count);
    updateTimer();
}

// What is written is noted where it is written (any thread, the lock often held) and read here, once, in the main thread's
// next turn: nothing goes round while nothing changes, and only the addresses that were named are read
void SequenceFeedback::queueFlush()
{
    if (m_flushQueued) return;
    m_flushQueued = true;
    QMetaObject::invokeMethod(this, &SequenceFeedback::flushChanges, Qt::QueuedConnection);
}

void SequenceFeedback::parameterWritten(quint64 holder, const QString &path)
{
    std::lock_guard<std::mutex> lk(m_pendingMutex);
    m_pendingParams.insert({holder, path});
    queueFlush();
}

void SequenceFeedback::layerChanged(quint64 layer)
{
    std::lock_guard<std::mutex> lk(m_pendingMutex);
    m_pendingLayers.insert(layer);
    queueFlush();
}

void SequenceFeedback::addressChanged(const QString &address)
{
    std::lock_guard<std::mutex> lk(m_pendingMutex);
    m_pendingAddresses.insert(address);
    queueFlush();
}

void SequenceFeedback::flushChanges()
{
    QSet<std::pair<quint64, QString>> params;
    QSet<quint64> layers;
    QSet<QString> addresses;
    {
        std::lock_guard<std::mutex> lk(m_pendingMutex);
        params.swap(m_pendingParams);
        layers.swap(m_pendingLayers);
        addresses.swap(m_pendingAddresses);
        m_flushQueued = false;
    }
    if (!m_enabled || m_targets.empty()) return;
    std::vector<osc::Message> changed;
    {
        Engine::Lock lock(&m_e->mutex());
        QSet<QString> seen; // an address once, whatever named it
        const auto take = [&](const QString &path, const OscNode &node) {
            if (m_sequencePaths.contains(path) || seen.contains(path)) return;
            seen.insert(path);
            osc::Message message;
            if (readNode(m_lastValues, false, path, node, &message)) changed.push_back(std::move(message));
        };
        for (const auto &key : params)
            for (const QString &address : m_namespace.addressesOf(key.first, key.second))
                if (const OscNode *node = m_namespace.node(address)) take(address, *node);
        for (const quint64 layer : layers)
            for (const QString &prefix : m_namespace.layerPrefixes(layer)) m_namespace.forEachUnder(prefix, take);
        for (const QString &address : addresses) m_namespace.forEachUnder(address, take);
    }
    int count = 0;
    for (const auto &message : changed) transmit(message, true, false, &count);
    if (count) emit sent(count);
}
