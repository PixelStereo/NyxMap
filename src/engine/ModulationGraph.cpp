#include "ModulationGraph.h"
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
namespace
{
const char *const names[] = {"Audio",           "OSC",    "MIDI",   "Constant", "Sine", "Add", "Multiply",
                             "Below Threshold", "Select", "Smooth", "Clamp"};
double finite(const QJsonObject &o, const char *key, double def, double lo = -1e6, double hi = 1e6)
{
    const double v = o.value(QLatin1String(key)).toDouble(def);
    return std::isfinite(v) ? std::clamp(v, lo, hi) : def;
}
} // namespace
QString ModulationNode::typeName(Type t)
{
    return int(t) >= 0 && int(t) <= Clamp ? QString::fromLatin1(names[int(t)]) : QStringLiteral("Unknown");
}
QString ModulationNode::label() const
{
    return name.isEmpty() ? typeName(type) + QStringLiteral(" %1").arg(id) : name;
}
QStringList ModulationNode::ports() const
{
    if (type == Add || type == Multiply)
        return {QStringLiteral("A"), QStringLiteral("B")};
    if (type == Select)
        return {QStringLiteral("Condition"), QStringLiteral("If true"), QStringLiteral("If false")};
    if (type == Below || type == Smooth || type == Clamp)
        return {QStringLiteral("Signal")};
    return {};
}
QJsonObject ModulationNode::toJson() const
{
    QJsonArray in;
    for (int i : inputs)
        in.append(i);
    return {{"id", id},
            {"type", int(type)},
            {"name", name},
            {"x", position.x()},
            {"y", position.y()},
            {"inputs", in},
            {"value", value},
            {"frequency", frequency},
            {"amplitude", amplitude},
            {"offset", offset},
            {"threshold", threshold},
            {"hysteresis", hysteresis},
            {"seconds", seconds},
            {"low", low},
            {"high", high},
            {"audio_input", audioInput},
            {"audio_feature", audioFeature},
            {"address", address},
            {"port", port},
            {"channel", channel},
            {"number", number},
            {"argument", argument}};
}
ModulationNode ModulationNode::fromJson(const QJsonObject &o)
{
    ModulationNode n;
    n.id = o.value("id").toInt();
    const int t = o.value("type").toInt(-1);
    n.type = Type(t);
    n.name = o.value("name").toString();
    n.position = {finite(o, "x", 0), finite(o, "y", 0)};
    const QJsonArray in = o.value("inputs").toArray();
    for (int i = 0; i < 3; ++i)
        n.inputs[size_t(i)] = in.at(i).toInt();
    n.value = finite(o, "value", 0);
    n.frequency = finite(o, "frequency", .3, 0, 100);
    n.amplitude = finite(o, "amplitude", .4);
    n.offset = finite(o, "offset", .5);
    n.threshold = finite(o, "threshold", .2);
    n.hysteresis = finite(o, "hysteresis", .04, 0, 1e6);
    n.seconds = finite(o, "seconds", .15, 0, 3600);
    n.low = finite(o, "low", 20, 20, 20000);
    n.high = finite(o, "high", 180, 20, 20000);
    if (n.low > n.high)
        std::swap(n.low, n.high);
    n.audioInput = o.value("audio_input").toBool(true);
    n.audioFeature = std::clamp(o.value("audio_feature").toInt(), 0, 2);
    n.address = o.value("address").toString(QStringLiteral("/control"));
    n.port = o.value("port").toString();
    n.channel = std::clamp(o.value("channel").toInt(), 0, 16);
    n.number = std::clamp(o.value("number").toInt(7), 0, 127);
    n.argument = std::clamp(o.value("argument").toInt(), 0, 1024);
    return n;
}
const ModulationNode *ModulationGraph::find(int id) const
{
    for (const auto &n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}
bool ModulationGraph::validate(QString *error) const
{
    auto fail = [&](const QString &s)
    {
        if (error)
            *error = s;
        return false;
    };
    if (nodes.empty() || nodes.size() > MaxNodes)
        return fail(QStringLiteral("A graph needs 1–128 nodes."));
    QSet<int> ids;
    for (const auto &n : nodes)
    {
        if (n.id <= 0 || ids.contains(n.id) || int(n.type) < 0 || n.type > ModulationNode::Clamp)
            return fail(QStringLiteral("Invalid or duplicate node."));
        ids.insert(n.id);
        if (!std::isfinite(n.position.x()) || !std::isfinite(n.position.y()) || n.frequency < 0 ||
            n.hysteresis < 0 || n.seconds < 0 || n.low < 20 || n.high > 20000 || n.low > n.high ||
            n.channel < 0 || n.channel > 16 || n.number < 0 || n.number > 127 || n.argument < 0 ||
            n.audioFeature < 0 || n.audioFeature > 2)
            return fail(QStringLiteral("Node settings are outside their valid range."));
        for (double v :
             {n.value, n.frequency, n.amplitude, n.offset, n.threshold, n.hysteresis, n.seconds, n.low, n.high})
            if (!std::isfinite(v))
                return fail(QStringLiteral("Node values must be finite."));
    }
    const auto *out = find(output);
    if (!out || out->condition())
        return fail(QStringLiteral("Choose a numeric output node."));
    QSet<int> visiting, done;
    std::function<bool(int)> visit = [&](int id)
    {
        if (done.contains(id))
            return true;
        if (visiting.contains(id))
            return false;
        visiting.insert(id);
        const auto *n = find(id);
        for (int p = 0; p < n->ports().size(); ++p)
        {
            const int src = n->inputs[size_t(p)];
            if (!src)
                continue;
            const auto *a = find(src);
            if (!a || a->condition() != n->acceptsCondition(p) || !visit(src))
                return false;
        }
        visiting.remove(id);
        done.insert(id);
        return true;
    };
    for (const auto &n : nodes)
        if (!visit(n.id))
            return fail(QStringLiteral("Invalid connection: cycle, missing node or incompatible port type."));
    if (error)
        error->clear();
    return true;
}
bool ModulationGraph::canConnect(int source, int target, int port) const
{
    ModulationGraph g = *this;
    for (auto &n : g.nodes)
        if (n.id == target)
        {
            if (port < 0 || port >= n.ports().size())
                return false;
            n.inputs[size_t(port)] = source;
            return g.validate();
        }
    return false;
}
QJsonObject ModulationGraph::toJson() const
{
    QJsonArray a;
    for (const auto &n : nodes)
        a.append(n.toJson());
    return {{"version", 1}, {"output", output}, {"nodes", a}};
}
ModulationGraph ModulationGraph::fromJson(const QJsonObject &o)
{
    ModulationGraph g;
    g.output = o.value("output").toInt();
    const auto a = o.value("nodes").toArray();
    if (a.size() > int(MaxNodes) || o.value("version").toInt(1) != 1)
        return {};
    for (const auto &v : a)
        g.nodes.push_back(ModulationNode::fromJson(v.toObject()));
    return g;
}
ModulationGraph ModulationGraph::example()
{
    ModulationGraph g;
    auto add = [&](int id, ModulationNode::Type t, double x, double y, std::array<int, 3> in = {{0, 0, 0}})
    {
        ModulationNode n;
        n.id = id;
        n.type = t;
        n.position = {x, y};
        n.inputs = in;
        g.nodes.push_back(n);
    };
    add(1, ModulationNode::Audio, 0, 0);
    add(2, ModulationNode::Osc, 0, 160);
    g.nodes.back().value = .2;
    add(3, ModulationNode::Midi, 0, 320);
    g.nodes.back().value = .7;
    add(4, ModulationNode::Add, 250, 0, {{1, 2, 0}});
    add(5, ModulationNode::Multiply, 500, 0, {{4, 3, 0}});
    add(6, ModulationNode::Below, 250, 280, {{3, 0, 0}});
    add(7, ModulationNode::Sine, 0, 510);
    add(8, ModulationNode::Select, 500, 280, {{6, 7, 5}});
    add(9, ModulationNode::Smooth, 500, 530, {{8, 0, 0}});
    add(10, ModulationNode::Clamp, 250, 580, {{9, 0, 0}});
    g.output = 10;
    return g;
}
bool ModulationProcessor::configure(const ModulationGraph &g)
{
    const auto previous = m_graph;
    const auto oldValues = m_values, oldHeld = m_held, oldPhases = m_phases;
    const auto oldStarted = m_started;
    m_graph = {};
    m_order.clear();
    m_inputs.clear();
    m_values.clear();
    m_held.clear();
    m_phases.clear();
    m_started.clear();
    m_output = -1;
    if (!g.validate())
        return false;
    m_graph = g;
    const int size = int(g.nodes.size());
    m_values.assign(size, 0);
    m_held.resize(size);
    m_phases.assign(size, 0);
    m_started.assign(size, false);
    m_inputs.resize(size);
    auto index = [&](int id)
    {
        for (int i = 0; i < size; ++i)
            if (g.nodes[size_t(i)].id == id)
                return i;
        return -1;
    };
    std::vector<bool> seen(size, false);
    std::function<void(int)> visit = [&](int i)
    {
        if (seen[size_t(i)])
            return;
        seen[size_t(i)] = true;
        for (int k : m_inputs[size_t(i)])
            if (k >= 0)
                visit(k);
        m_order.push_back(i);
    };
    for (int i = 0; i < size; ++i)
    {
        m_held[size_t(i)] = g.nodes[size_t(i)].value;
        m_inputs[size_t(i)] = {{-1, -1, -1}};
        for (int p = 0; p < g.nodes[size_t(i)].ports().size(); ++p)
            m_inputs[size_t(i)][size_t(p)] = index(g.nodes[size_t(i)].inputs[size_t(p)]);
    }
    for (int i = 0; i < size; ++i)
    {
        visit(i);
        const auto &n = g.nodes[size_t(i)];
        for (size_t j = 0; j < previous.nodes.size(); ++j)
        {
            if (previous.nodes[j].id != n.id)
                continue;
            auto a = previous.nodes[j].toJson(), b = n.toJson();
            for (const char *key : {"x", "y", "name"})
            {
                a.remove(QLatin1String(key));
                b.remove(QLatin1String(key));
            }
            if (a == b)
            {
                m_values[size_t(i)] = oldValues[j];
                m_held[size_t(i)] = oldHeld[j];
                m_phases[size_t(i)] = oldPhases[j];
                m_started[size_t(i)] = oldStarted[j];
            }
            break;
        }
    }
    m_output = index(g.output);
    return true;
}
void ModulationProcessor::feed(int id, double value)
{
    if (!std::isfinite(value))
        return;
    for (size_t i = 0; i < m_graph.nodes.size(); ++i)
        if (m_graph.nodes[i].id == id)
        {
            m_held[i] = value;
            return;
        }
}
std::optional<double> ModulationProcessor::step(double dt, const std::function<double(const ModulationNode &)> &audio)
{
    if (m_output < 0)
        return {};
    dt = std::isfinite(dt) ? std::max(0., dt) : 0;
    for (int idx : m_order)
    {
        const size_t i = size_t(idx);
        const auto &n = m_graph.nodes[i];
        auto in = [&](int p)
        {
            const int j = m_inputs[i][size_t(p)];
            return j < 0 ? 0. : m_values[size_t(j)];
        };
        double value = 0;
        switch (n.type)
        {
        case ModulationNode::Audio:
            value = audio(n);
            break;
        case ModulationNode::Osc:
        case ModulationNode::Midi:
            value = m_held[i];
            break;
        case ModulationNode::Constant:
            value = n.value;
            break;
        case ModulationNode::Sine:
            value = n.offset + n.amplitude * std::sin(6.283185307179586 * m_phases[i]);
            m_phases[i] = std::fmod(m_phases[i] + dt * n.frequency, 1.);
            break;
        case ModulationNode::Add:
            value = in(0) + in(1);
            break;
        case ModulationNode::Multiply:
            value = in(0) * in(1);
            break;
        case ModulationNode::Below:
            value =
                in(0) < n.threshold + (m_started[i] ? (m_values[i] > .5 ? n.hysteresis / 2 : -n.hysteresis / 2) : 0);
            break;
        case ModulationNode::Select:
            value = in(0) > .5 ? in(1) : in(2);
            break;
        case ModulationNode::Smooth:
            value = !m_started[i] || n.seconds <= 0
                        ? in(0)
                        : m_values[i] + (in(0) - m_values[i]) * -std::expm1(-dt / n.seconds);
            break;
        case ModulationNode::Clamp:
            value = std::clamp(in(0), 0., 1.);
            break;
        }
        m_values[i] = std::isfinite(value) ? value : 0;
        m_started[i] = true;
    }
    return m_values[size_t(m_output)];
}
