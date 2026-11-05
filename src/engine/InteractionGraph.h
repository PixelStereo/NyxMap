#pragma once
#include "Interactions.h"
#include <map>
#include <cmath>
#include <QJsonArray>
#include <set>
#include <tuple>
#include <vector>

// Edited off the render path. Stable node IDs identify state independently of
// layout and fan-out. Only explicit combiners may have more than one input.
struct InteractionGraph {
    enum class Kind { Source, Filter, Destination, Add, Maximum, Multiply, Minimum, Mix, Select };
    struct Node {
        quint64 id = 0;
        Kind kind = Kind::Source;
        Interaction settings;
        double mix = 0.5;
        int selectedInput = 0;
        double x = 0, y = 0;
        bool positioned = false;
        quint64 mapping = 0; // reuse the already computed scalar mapping output
    };
    struct Edge { quint64 from = 0, to = 0; int port = 0; };
    std::vector<Node> nodes;
    std::vector<Edge> edges;

    bool compile(std::vector<size_t> *order, QString *error = nullptr) const
    {
        auto fail = [&](const char *reason) { if (error) *error = QString::fromLatin1(reason); return false; };
        std::map<quint64, size_t> index;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (!nodes[i].id || !index.emplace(nodes[i].id, i).second) return fail("Duplicate or zero node ID");
            if (int(nodes[i].kind) < 0 || int(nodes[i].kind) > int(Kind::Select)) return fail("Invalid node kind");
        }
        std::vector<size_t> degree(nodes.size());
        std::vector<std::vector<size_t>> next(nodes.size());
        std::set<std::tuple<quint64, quint64, int>> unique;
        std::set<std::pair<quint64, int>> ports;
        for (const Edge &edge : edges) {
            auto a = index.find(edge.from), b = index.find(edge.to);
            if (a == index.end() || b == index.end()) return fail("Missing cable endpoint");
            if (edge.port < 0 || edge.port >= 1024 || !ports.emplace(edge.to, edge.port).second) return fail("Invalid or occupied input port");
            if (!unique.emplace(edge.from, edge.to, edge.port).second) return fail("Duplicate cable");
            const auto from = nodes[a->second].kind, to = nodes[b->second].kind;
            if ((int(to) < int(Kind::Add) && edge.port != 0) || ((to == Kind::Mix || to == Kind::Select) && edge.port > 1)) return fail("Invalid input port");
            if (from == Kind::Destination || to == Kind::Source) return fail("Invalid cable direction");
            if (++degree[b->second] > 1 && int(to) < int(Kind::Add))
                return fail("Multiple inputs require a combiner");
            if ((to == Kind::Mix || to == Kind::Select) && degree[b->second] > 2)
                return fail("This combiner accepts two inputs");
            next[a->second].push_back(b->second);
        }
        std::vector<size_t> result;
        for (size_t i = 0; i < nodes.size(); ++i) if (!degree[i]) result.push_back(i);
        for (size_t i = 0; i < result.size(); ++i)
            for (size_t child : next[result[i]]) if (!--degree[child]) result.push_back(child);
        if (result.size() != nodes.size()) return fail("Cable creates a cycle");
        if (order) *order = std::move(result);
        if (error) error->clear();
        return true;
    }
    QJsonObject toJson() const
    {
        QJsonArray ns, es;
        for (const auto &node : nodes)
            ns.append(QJsonObject{{"id", QString::number(node.id)}, {"kind", int(node.kind)},
                                  {"settings", node.settings.toJson()}, {"mix", node.mix}, {"selected_input", node.selectedInput}, {"x", node.x}, {"y", node.y}, {"positioned", node.positioned}, {"mapping", QString::number(node.mapping)}});
        for (const auto &edge : edges)
            es.append(QJsonObject{{"from", QString::number(edge.from)}, {"to", QString::number(edge.to)}, {"port", edge.port}});
        return {{"version", 1}, {"nodes", ns}, {"edges", es}};
    }
    static bool fromJson(const QJsonObject &json, InteractionGraph *result, QString *error = nullptr)
    {
        if (!result || json.value("version").toInt(-1) != 1 ||
            !json.value("nodes").isArray() || !json.value("edges").isArray()) {
            if (error) *error = QStringLiteral("Invalid graph document");
            return false;
        }
        InteractionGraph candidate;
        for (const auto &value : json.value("nodes").toArray()) {
            const auto object = value.toObject();
            candidate.nodes.push_back({object.value("id").toString().toULongLong(),
                Kind(object.value("kind").toInt(-1)), Interaction::fromJson(object.value("settings").toObject()),
                object.value("mix").toDouble(.5), object.value("selected_input").toInt(), object.value("x").toDouble(), object.value("y").toDouble(), object.value("positioned").toBool(), object.value("mapping").toString().toULongLong()});
        }
        for (const auto &value : json.value("edges").toArray()) {
            const auto object = value.toObject();
            candidate.edges.push_back({object.value("from").toString().toULongLong(),
                                       object.value("to").toString().toULongLong(), object.value("port").toInt()});
        }
        if (!candidate.compile(nullptr, error)) return false;
        *result = std::move(candidate);
        return true;
    }
    bool connect(quint64 from, quint64 to, QString *error = nullptr, int port = -1)
    {
        if (port < 0) {
            port = 0;
            while (std::any_of(edges.begin(), edges.end(), [&](const auto &e) { return e.to == to && e.port == port; })) ++port;
        }
        edges.push_back({from, to, port});
        if (compile(nullptr, error)) return true;
        edges.pop_back();
        return false;
    }
};

// Scalar execution foundation: topology and storage are prepared at edit time.
// Sources are supplied by the engine; disconnected nodes produce no value.
class InteractionGraphProcessor {
public:
    bool configure(const InteractionGraph &graph, QString *error = nullptr)
    {
        std::vector<size_t> order;
        if (!graph.compile(&order, error)) return false;
        // Preserve unaffected signal histories across edits; rewiring or changing
        // a source resets its descendants. Layout is kept outside this signature.
        std::map<quint64, size_t> index;
        std::vector<std::vector<size_t>> inputs(graph.nodes.size());
        for (size_t i = 0; i < graph.nodes.size(); ++i) index.emplace(graph.nodes[i].id, i);
        for (const auto &edge : graph.edges) {
            auto &ports = inputs[index.at(edge.to)];
            ports.resize(std::max(ports.size(), size_t(edge.port + 1)), missing);
            ports[size_t(edge.port)] = index.at(edge.from);
        }
        std::vector<InteractionProcessor> processors(graph.nodes.size());
        std::vector<std::optional<double>> values(graph.nodes.size());
        std::vector<bool> kept(graph.nodes.size());
        for (size_t i : order) {
            const auto &node = graph.nodes[i];
            auto old = m_index.find(node.id);
            if (old == m_index.end()) continue;
            size_t j = old->second;
            const auto &before = m_graph.nodes[j];
            auto binding = [](const Interaction &settings) {
                auto json = settings.toJson(); json.remove("name"); json.remove("id"); return json;
            };
            bool same = node.kind == before.kind && inputs[i].size() == m_inputs[j].size();
            if (node.kind == InteractionGraph::Kind::Source)
                same = same && node.mapping == before.mapping && binding(node.settings) == binding(before.settings);
            for (size_t k = 0; same && k < inputs[i].size(); ++k)
                same = inputs[i][k] == missing || m_inputs[j][k] == missing
                    ? inputs[i][k] == m_inputs[j][k]
                    : kept[inputs[i][k]] && graph.nodes[inputs[i][k]].id == m_graph.nodes[m_inputs[j][k]].id;
            if (!same) continue;
            kept[i] = true;
            processors[i] = m_processors[j];
            values[i] = m_values[j];
        }
        m_graph = graph;
        m_order = std::move(order);
        m_index = std::move(index);
        m_inputs = std::move(inputs);
        m_values = std::move(values);
        m_processors = std::move(processors);
        m_eventDirty.assign(graph.nodes.size(), false);
        return true;
    }
    void input(quint64 id, double value)
    {
        auto at = m_index.find(id);
        if (at != m_index.end() && m_graph.nodes[at->second].kind == InteractionGraph::Kind::Source && std::isfinite(value))
        {
            m_values[at->second] = value;
            m_eventDirty[at->second] = true;
        }
    }
    void clearInput(quint64 id) {
        auto it = m_index.find(id);
        if (it != m_index.end()) m_values[it->second].reset();
    }
    double hit(quint64 id, const OnsetFlux &flux, double dt)
    {
        auto at = m_index.find(id);
        if (at == m_index.end()) return 0;
        return m_processors[at->second].hit(flux, m_graph.nodes[at->second].settings, dt);
    }
    void step(double dt, bool eventOnly = false)
    {
        using Kind = InteractionGraph::Kind;
        for (size_t i : m_order) {
            const auto &node = m_graph.nodes[i];
            if (node.kind == Kind::Source) {
                if (!node.settings.enabled) m_values[i].reset();
                continue;
            }
            bool fresh = false;
            for (size_t parent : m_inputs[i]) if (parent != missing) fresh = fresh || m_eventDirty[parent];
            m_eventDirty[i] = fresh;
            if (eventOnly && !fresh) continue;
            if (eventOnly && node.kind == Kind::Filter) {
                const bool timed = std::any_of(node.settings.filters.begin(), node.settings.filters.end(), [](const auto &f) {
                    return f.enabled && f.kind != InteractionFilter::Toggle &&
                           f.kind != InteractionFilter::Curve && f.kind != InteractionFilter::Math &&
                           f.kind != InteractionFilter::Clamp && f.kind != InteractionFilter::Steps && f.kind != InteractionFilter::Threshold;
                });
                if (timed) { m_eventDirty[i] = false; continue; }
            }
            std::optional<double> value;
            if (node.kind == Kind::Mix || node.kind == Kind::Select) {
                auto inputAt = [&](size_t port) -> std::optional<double> {
                    return port >= m_inputs[i].size() || m_inputs[i][port] == missing
                        ? std::nullopt : m_values[m_inputs[i][port]];
                };
                if (node.kind == Kind::Select) value = inputAt(node.selectedInput == 0 ? 0 : 1);
                else {
                    const auto a = inputAt(0), b = inputAt(1);
                    const double mix = std::clamp(node.mix, 0.0, 1.0);
                    if (a && b) value = *a * (1 - mix) + *b * mix;
                }
                m_values[i] = value && std::isfinite(*value) ? value : std::nullopt;
                continue;
            }
            for (size_t parent : m_inputs[i]) {
                if (parent == missing || !m_values[parent]) continue;
                if (!value) value = m_values[parent];
                else if (node.kind == Kind::Add) *value += *m_values[parent];
                else if (node.kind == Kind::Maximum) *value = std::max(*value, *m_values[parent]);
                else if (node.kind == Kind::Minimum) *value = std::min(*value, *m_values[parent]);
                else if (node.kind == Kind::Multiply) *value *= *m_values[parent];
            }
            if (node.kind == Kind::Filter && node.settings.enabled && value) {
                m_processors[i].input(node.settings, *value);
                value = m_processors[i].step(node.settings, dt);
            }
            m_values[i] = value && std::isfinite(*value) ? value : std::nullopt;
        }
    }
    bool eventOutput(quint64 id) const {
        auto at = m_index.find(id);
        return at != m_index.end() && m_eventDirty[at->second];
    }
    void clearEvent() { std::fill(m_eventDirty.begin(), m_eventDirty.end(), false); }
    std::optional<double> value(quint64 id) const
    {
        auto at = m_index.find(id);
        return at == m_index.end() ? std::nullopt : m_values[at->second];
    }
private:
    static constexpr size_t missing = size_t(-1);
    InteractionGraph m_graph;
    std::map<quint64, size_t> m_index;
    std::vector<size_t> m_order;
    std::vector<std::vector<size_t>> m_inputs;
    std::vector<std::optional<double>> m_values;
    std::vector<InteractionProcessor> m_processors;
    std::vector<bool> m_eventDirty;
};
