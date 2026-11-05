#pragma once
#include "Engine.h"
#include "InteractionGraphView.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

class SetInteractionGraph : public QUndoCommand {
public:
    SetInteractionGraph(Engine *engine, InteractionGraph after, const QString &text)
        : QUndoCommand(text), m_engine(engine), m_before(engine->interactionGraph()), m_after(std::move(after)) {}
    void undo() override { m_engine->setInteractionGraph(m_before); }
    void redo() override { m_engine->setInteractionGraph(m_after); }
private:
    Engine *m_engine;
    InteractionGraph m_before, m_after;
};

class InteractionGraphWorkspace : public QWidget {
public:
    InteractionGraphWorkspace(Engine *engine, QUndoStack *undo, QWidget *parent = nullptr)
        : QWidget(parent), m_engine(engine), m_undo(undo)
    {
        setObjectName(QStringLiteral("interactionGraphWorkspace"));
        auto *layout = new QVBoxLayout(this);
        auto *add = new QToolButton;
        add->setText(QStringLiteral("+ Add block"));
        add->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(add); add->setMenu(menu);
        using Kind = InteractionGraph::Kind;
        for (auto kind : {Kind::Source, Kind::Filter, Kind::Destination, Kind::Add, Kind::Multiply, Kind::Minimum, Kind::Maximum, Kind::Mix, Kind::Select}) {
            const QString label = QStringList{"Source", "Filter", "Destination", "Add", "Maximum", "Multiply", "Minimum", "Mix", "Select"}.value(int(kind));
            menu->addAction(label, this, [this, kind] {
                auto graph = m_engine->interactionGraph();
                quint64 id = 1;
                for (const auto &node : graph.nodes) id = std::max(id, node.id + 1);
                Interaction settings;
                if (kind == Kind::Filter) settings.filters = {InteractionStage::made(InteractionFilter::Math)};
                graph.nodes.push_back({id, kind, settings});
                m_selected = id;
                commit(std::move(graph), QStringLiteral("Add Graph Block"));
            });
        }
        layout->addWidget(add, 0, Qt::AlignLeft);
        m_error = new QLabel;
        m_error->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum); m_error->setWordWrap(true); layout->addWidget(m_error);
        auto *split = new QSplitter;
        m_canvas = new InteractionGraphView;
        split->addWidget(m_canvas);
        m_inspector = new QWidget;
        m_inspectorLayout = new QVBoxLayout(m_inspector);
        split->addWidget(m_inspector); split->setStretchFactor(0, 3);
        layout->addWidget(split, 1);
        m_canvas->selected = [this](quint64 id) { m_selected = id; rebuildInspector(); };
        m_canvas->moved = [this](quint64 id, QPointF position) {
            edit([&](auto &node) { node.x = position.x(); node.y = position.y(); node.positioned = true; }, id);
        };
        m_canvas->connected = [this](quint64 a, quint64 b, int port) {
            auto graph = m_engine->interactionGraph(); QString error;
            if (!graph.connect(a, b, &error, port)) { m_error->setText(error); return; }
            commit(std::move(graph), QStringLiteral("Connect Graph Blocks"));
        };
        m_canvas->removed = [this](quint64 id) {
            auto graph = m_engine->interactionGraph();
            graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(), [id](const auto &n) { return n.id == id; }), graph.nodes.end());
            graph.edges.erase(std::remove_if(graph.edges.begin(), graph.edges.end(), [id](const auto &e) { return e.from == id || e.to == id; }), graph.edges.end());
            commit(std::move(graph), QStringLiteral("Remove Graph Block"));
        };
        m_canvas->disconnected = [this](quint64 a, quint64 b, int port) {
            auto graph = m_engine->interactionGraph();
            graph.edges.erase(std::remove_if(graph.edges.begin(), graph.edges.end(), [=](const auto &e) { return e.from == a && e.to == b && e.port == port; }), graph.edges.end());
            commit(std::move(graph), QStringLiteral("Disconnect Graph Blocks"));
        };
        connect(engine, &Engine::interactionsChanged, this, [this] {
            if (m_refreshPending) return;
            m_refreshPending = true;
            QTimer::singleShot(0, this, [this] { m_refreshPending = false; refresh(); });
        });
        refresh();
    }
    void addBranch(quint64 mapping, bool filtered) {
        auto graph = m_engine->interactionGraph();
        quint64 next = 1, from = 0;
        for (const auto &n : graph.nodes) {
            next = std::max(next, n.id + 1);
            if (n.mapping == mapping && n.kind == InteractionGraph::Kind::Source) from = n.id;
        }
        if (!from) {
            const auto definitions = m_engine->interactions();
            const auto it = std::find_if(definitions.begin(), definitions.end(), [=](const auto &x) { return x.id == mapping; });
            if (it == definitions.end() || it->isAction() || !it->xyInputs.empty()) return;
            Interaction settings;
            settings.name = it->name;
            from = next++;
            graph.nodes.push_back({from, InteractionGraph::Kind::Source, settings});
            graph.nodes.back().mapping = mapping;
        }
        appendBranch(std::move(graph), from, next, filtered);
    }
private:
    void appendBranch(InteractionGraph graph, quint64 from, quint64 next, bool filtered) {
        if (filtered) {
            Interaction settings;
            settings.filters = {InteractionStage::made(InteractionFilter::Math)};
            settings.filters.front().op = MathOp::Multiply;
            settings.filters.front().a = 1;
            graph.nodes.push_back({next, InteractionGraph::Kind::Filter, settings});
            graph.edges.push_back({from, next});
            from = next++;
        }
        graph.nodes.push_back({next, InteractionGraph::Kind::Destination, {}});
        graph.edges.push_back({from, next});
        m_selected = next;
        commit(std::move(graph), QStringLiteral("Add mapping destination"));
    }

private:
    void commit(InteractionGraph graph, const QString &text) {
        QString error;
        if (!graph.compile(nullptr, &error)) { m_error->setText(error); return; }
        m_error->clear();
        m_undo->push(new SetInteractionGraph(m_engine, std::move(graph), text));
    }
    void edit(const std::function<void(InteractionGraph::Node &)> &fn, quint64 id = 0) {
        auto graph = m_engine->interactionGraph();
        for (auto &node : graph.nodes) if (node.id == (id ? id : m_selected)) { fn(node); break; }
        commit(std::move(graph), QStringLiteral("Edit Graph Block"));
    }
    void refresh() { m_canvas->setGraph(m_engine->interactionGraph()); rebuildInspector(); }
    void rebuildInspector() {
        while (auto *item = m_inspectorLayout->takeAt(0)) { if (item->widget()) { item->widget()->hide(); item->widget()->deleteLater(); } delete item; }
        auto graph = m_engine->interactionGraph();
        auto at = std::find_if(graph.nodes.begin(), graph.nodes.end(), [this](const auto &n) { return n.id == m_selected; });
        if (at == graph.nodes.end()) { m_inspectorLayout->addWidget(new QLabel(QStringLiteral("Select a block to edit its settings."))); return; }
        const auto node = *at;
        auto *box = new QWidget; auto *form = new QFormLayout(box);
        m_inspectorLayout->addWidget(box);
        auto text = [&](const QString &label, QString value, std::function<void(InteractionGraph::Node &, QString)> set) {
            auto *field = new QLineEdit(value); form->addRow(label, field);
            connect(field, &QLineEdit::editingFinished, this, [this, field, set, id = node.id] { edit([&](auto &n) { set(n, field->text()); }, id); });
        };
        auto number = [&](const QString &label, double value, std::function<void(InteractionGraph::Node &, double)> set) {
            auto *field = new QDoubleSpinBox; field->setRange(-1e9, 1e9); field->setDecimals(5); field->setValue(value); form->addRow(label, field);
            connect(field, &QDoubleSpinBox::editingFinished, this, [this, field, set, id = node.id] { edit([&](auto &n) { set(n, field->value()); }, id); });
        };
        auto parameterPicker = [&](bool input) {
            auto *picker = new QToolButton;
            picker->setPopupMode(QToolButton::InstantPopup);
            picker->setText(input ? node.settings.sourceParam : node.settings.param);
            if (picker->text().isEmpty()) picker->setText(QStringLiteral("Choose a parameter…"));
            auto *menu = new QMenu(picker); picker->setMenu(menu);
            for (int index = -1; index < m_engine->layerCount(); ++index) {
                quint64 id = 0;
                QString title = QStringLiteral("Composition");
                if (index >= 0) {
                    Engine::Lock lock(&m_engine->mutex());
                    auto *layer = m_engine->layer(index);
                    if (!layer) continue;
                    id = layer->id; title = layer->name;
                }
                auto *owner = menu->addMenu(title);
                for (const auto &parameter : m_engine->parameters(id)) {
                    if (parameter.type == ParamInfo::Type::Text || !(parameter.access & (input ? ParamInfo::Read : ParamInfo::Write))) continue;
                    owner->addAction(parameter.label, this, [this, id, path = parameter.path, input] {
                        edit([&](auto &n) {
                            if (input) { n.settings.sourceLayer = id; n.settings.sourceParam = path; }
                            else { n.settings.layer = id; n.settings.param = path; }
                        });
                    });
                }
            }
            form->addRow(input ? QStringLiteral("Value to follow") : QStringLiteral("Destination"), picker);
        };
        text("Name", node.settings.name, [](auto &n, QString value) { n.settings.name = value; });
        using Kind = InteractionGraph::Kind;
        if (node.kind != Kind::Destination) {
            for (bool filtered : {false, true}) {
                auto *branch = new QPushButton(filtered ? QStringLiteral("+ Filter → destination") : QStringLiteral("+ Destination"));
                form->addRow(branch);
                connect(branch, &QPushButton::clicked, this, [this, id = node.id, filtered] {
                    auto graph = m_engine->interactionGraph();
                    quint64 next = 1;
                    for (const auto &n : graph.nodes) next = std::max(next, n.id + 1);
                    appendBranch(std::move(graph), id, next, filtered);
                });
            }
        }
        if (node.mapping) form->addRow(new QLabel(QStringLiteral("Output of the original mapping")));
        else if (node.kind == Kind::Source) {
            auto *source = new QComboBox;
            for (int k = 0; k < kInteractionSourceCount; ++k) source->addItem(interactionSourceName(InteractionSource(k)));
            source->setCurrentIndex(int(node.settings.source)); form->addRow("Source", source);
            connect(source, qOverload<int>(&QComboBox::activated), this, [this](int k) { edit([=](auto &n) { n.settings.source = InteractionSource(k); }); });
            if (node.settings.source == InteractionSource::Keyboard)
                text("Key", node.settings.key, [](auto &n, QString v) { n.settings.key = v; });
            if (node.settings.source == InteractionSource::Osc) {
                text("OSC address", node.settings.oscAddress, [](auto &n, QString v) { n.settings.oscAddress = v; });
                number("Argument", node.settings.oscArgument, [](auto &n, double v) { n.settings.oscArgument = std::clamp(int(v), 0, 63); });
            }
            if (node.settings.source == InteractionSource::Midi) {
                text("MIDI input (empty: all)", node.settings.midiPort, [](auto &n, QString v) { n.settings.midiPort = v; });
                number("Channel (0: all)", node.settings.midiChannel, [](auto &n, double v) { n.settings.midiChannel = std::clamp(int(v), 0, 16); });
                auto *message = new QComboBox;
                for (int k = 0; k < 4; ++k) message->addItem(midiMessageName(MidiMessageKind(k)));
                message->setCurrentIndex(int(node.settings.midiMessage)); form->addRow("Message", message);
                connect(message, qOverload<int>(&QComboBox::activated), this, [this](int k) { edit([=](auto &n) { n.settings.midiMessage = MidiMessageKind(k); }); });
                number("Number", node.settings.midiNumber, [](auto &n, double v) { n.settings.midiNumber = std::clamp(int(v), 0, 127); });
            }
            if (node.settings.source == InteractionSource::Audio) {
                auto *input = new QComboBox; input->addItems({"Program mix", "Audio input"});
                input->setCurrentIndex(node.settings.audioInput ? 1 : 0); form->addRow("Listen to", input);
                connect(input, qOverload<int>(&QComboBox::activated), this, [this](int k) { edit([=](auto &n) { n.settings.audioInput = k != 0; }); });
                auto *feature = new QComboBox; feature->addItems({"Frequency band", "Level", "Onset", "Hit"});
                feature->setCurrentIndex(int(node.settings.audioFeature)); form->addRow("Measure", feature);
                connect(feature, qOverload<int>(&QComboBox::activated), this, [this](int k) { edit([=](auto &n) { n.settings.audioFeature = Interaction::AudioFeature(k); }); });
                number("Low frequency (Hz)", node.settings.audioLow, [](auto &n, double v) { n.settings.audioLow = std::clamp(v, 0., 24000.); });
                number("High frequency (Hz)", node.settings.audioHigh, [](auto &n, double v) { n.settings.audioHigh = std::clamp(v, 0., 24000.); });
            }
            if (node.settings.source == InteractionSource::Internal) {
                parameterPicker(true);
            }
        } else if (node.kind == Kind::Destination) {
            parameterPicker(false);
        } else if (node.kind == Kind::Filter) {
            auto *kind = new QComboBox;
            for (int k = 0; k < kInteractionFilterCount; ++k) kind->addItem(interactionFilterName(InteractionFilter(k)));
            const auto stage = node.settings.filters.empty() ? InteractionStage::made(InteractionFilter::Math) : node.settings.filters.front();
            kind->setCurrentIndex(int(stage.kind)); form->addRow("Filter", kind);
            connect(kind, qOverload<int>(&QComboBox::activated), this, [this](int k) { edit([=](auto &n) { n.settings.filters = {InteractionStage::made(InteractionFilter(k))}; }); });
            if (stage.kind == InteractionFilter::Math) {
                auto *op = new QComboBox;
                for (int k = 0; k < kMathOpCount; ++k) op->addItem(mathOpName(MathOp(k)));
                op->setCurrentIndex(int(stage.op)); form->addRow("Operation", op);
                connect(op, qOverload<int>(&QComboBox::activated), this, [this](int k) { edit([=](auto &n) { n.settings.filters.front().op = MathOp(k); }); });
            }
            for (int k = 0; k < 4; ++k) {
                const auto setting = interactionFilterSetting(stage.kind, k);
                if (!setting.name.isEmpty()) number(setting.name + setting.suffix, stage.setting(k), [k](auto &n, double v) { n.settings.filters.front().setting(k) = v; });
            }
        } else if (node.kind == Kind::Mix) number("Mix (0: A, 1: B)", node.mix, [](auto &n, double v) { n.mix = std::clamp(v, 0., 1.); });
        else if (node.kind == Kind::Select) number("Input (0: A, 1: B)", node.selectedInput, [](auto &n, double v) { n.selectedInput = v >= .5 ? 1 : 0; });
        m_inspectorLayout->addStretch();
    }
    Engine *m_engine;
    QUndoStack *m_undo;
    InteractionGraphView *m_canvas;
    QWidget *m_inspector;
    QVBoxLayout *m_inspectorLayout;
    QLabel *m_error;
    quint64 m_selected = 0;
    bool m_refreshPending = false;
};
