#pragma once
#include <QJsonObject>
#include <QPointF>
#include <QStringList>
#include <array>
#include <functional>
#include <optional>
#include <vector>

// Bounded scalar/condition DAG; compiled once per edit, evaluated once per frame.
struct ModulationNode
{
    enum Type : int
    {
        Audio,
        Osc,
        Midi,
        Constant,
        Sine,
        Add,
        Multiply,
        Below,
        Select,
        Smooth,
        Clamp
    };
    int id = 0;
    Type type = Constant;
    QString name;
    QPointF position;
    std::array<int, 3> inputs{{0, 0, 0}};
    double value = 0, frequency = .3, amplitude = .4, offset = .5;
    double threshold = .2, hysteresis = .04, seconds = .15, low = 20, high = 180;
    bool audioInput = true;
    int audioFeature = 0; // band, level, onset
    QString address = QStringLiteral("/control"), port;
    int channel = 0, number = 7, argument = 0;
    bool condition() const
    {
        return type == Below;
    }
    static QString typeName(Type type);
    QString label() const;
    QStringList ports() const;
    bool acceptsCondition(int index) const
    {
        return type == Select && index == 0;
    }
    QJsonObject toJson() const;
    static ModulationNode fromJson(const QJsonObject &json);
};
struct ModulationGraph
{
    std::vector<ModulationNode> nodes;
    int output = 0;
    static constexpr size_t MaxNodes = 128;
    const ModulationNode *find(int id) const;
    bool validate(QString *error = nullptr) const;
    bool canConnect(int source, int target, int port) const;
    QJsonObject toJson() const;
    static ModulationGraph fromJson(const QJsonObject &json);
    static ModulationGraph example();
};
class ModulationProcessor
{
  public:
    bool configure(const ModulationGraph &graph);
    void feed(int node, double value);
    std::optional<double> step(double dt, const std::function<double(const ModulationNode &)> &audio);
    const std::vector<double> &values() const
    {
        return m_values;
    }

  private:
    ModulationGraph m_graph;
    std::vector<int> m_order;
    std::vector<std::array<int, 3>> m_inputs;
    std::vector<double> m_values, m_held, m_phases;
    std::vector<bool> m_started;
    int m_output = -1;
};
