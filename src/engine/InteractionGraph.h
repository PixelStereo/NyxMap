#pragma once
#include "Interactions.h"
#include <map>
#include <cmath>
#include <QJsonArray>
#include <set>
#include <vector>

// Edited off the render path. Stable node IDs identify state independently of
// layout and fan-out. Only explicit combiners may have more than one input.
struct InteractionGraph {
    enum class Kind { Source, Filter, Destination, Add, Maximum };
    struct Node {
        quint64 id = 0;
        Kind kind = Kind::Source;
        Interaction settings;
    };
    struct Edge { quint64 from = 0, to = 0; };
    std::vector<Node> nodes;
    std::vector<Edge> edges;

    bool compile(std::vector<size_t> *order, QString *error = nullptr) const
    {
        auto fail = [&](const char *reason) { if (error) *error = QString::fromLatin1(reason); return false; };
        std::map<quint64, size_t> index;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (!nodes[i].id || !index.emplace(nodes[i].id, i).second) return fail("Duplicate or zero node ID");
            if (int(nodes[i].kind) < 0 || int(nodes[i].kind) > int(Kind::Maximum)) return fail("Invalid node kind");
        }
        std::vector<size_t> degree(nodes.size());
        std::vector<std::vector<size_t>> next(nodes.size());
        std::set<std::pair<quint64, quint64>> unique;
        for (const Edge &edge : edges) {
            auto a = index.find(edge.from), b = index.find(edge.to);
            if (a == index.end() || b == index.end()) return fail("Missing cable endpoint");
            if (!unique.emplace(edge.from, edge.to).second) return fail("Duplicate cable");
            const auto from = nodes[a->second].kind, to = nodes[b->second].kind;
            if (from == Kind::Destination || to == Kind::Source) return fail("Invalid cable direction");
            if (++degree[b->second] > 1 && to != Kind::Add && to != Kind::Maximum)
                return fail("Multiple inputs require a combiner");
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
                                  {"settings", node.settings.toJson()}});
        for (const auto &edge : edges)
            es.append(QJsonObject{{"from", QString::number(edge.from)}, {"to", QString::number(edge.to)}});
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
                Kind(object.value("kind").toInt(-1)), Interaction::fromJson(object.value("settings").toObject())});
        }
        for (const auto &value : json.value("edges").toArray()) {
            const auto object = value.toObject();
            candidate.edges.push_back({object.value("from").toString().toULongLong(),
                                       object.value("to").toString().toULongLong()});
        }
        if (!candidate.compile(nullptr, error)) return false;
        *result = std::move(candidate);
        return true;
    }
    bool connect(quint64 from, quint64 to, QString *error = nullptr)
    {
        edges.push_back({from, to});
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
        m_graph = graph;
        m_order = std::move(order);
        m_index.clear();
        m_inputs.assign(graph.nodes.size(), {});
        m_values.assign(graph.nodes.size(), std::nullopt);
        m_processors.assign(graph.nodes.size(), {});
        for (size_t i = 0; i < graph.nodes.size(); ++i) m_index.emplace(graph.nodes[i].id, i);
        for (const auto &edge : graph.edges) m_inputs[m_index.at(edge.to)].push_back(m_index.at(edge.from));
        return true;
    }
    void input(quint64 id, double value)
    {
        auto at = m_index.find(id);
        if (at != m_index.end() && m_graph.nodes[at->second].kind == InteractionGraph::Kind::Source && std::isfinite(value))
            m_values[at->second] = value;
    }
    void step(double dt)
    {
        using Kind = InteractionGraph::Kind;
        for (size_t i : m_order) {
            const auto &node = m_graph.nodes[i];
            if (node.kind == Kind::Source) continue;
            std::optional<double> value;
            for (size_t parent : m_inputs[i]) {
                if (!m_values[parent]) continue;
                if (!value) value = m_values[parent];
                else if (node.kind == Kind::Add) *value += *m_values[parent];
                else if (node.kind == Kind::Maximum) *value = std::max(*value, *m_values[parent]);
            }
            if (node.kind == Kind::Filter && value) {
                m_processors[i].input(node.settings, *value);
                value = m_processors[i].step(node.settings, dt);
            }
            m_values[i] = value && std::isfinite(*value) ? value : std::nullopt;
        }
    }
    std::optional<double> value(quint64 id) const
    {
        auto at = m_index.find(id);
        return at == m_index.end() ? std::nullopt : m_values[at->second];
    }
private:
    InteractionGraph m_graph;
    std::map<quint64, size_t> m_index;
    std::vector<size_t> m_order;
    std::vector<std::vector<size_t>> m_inputs;
    std::vector<std::optional<double>> m_values;
    std::vector<InteractionProcessor> m_processors;
};
