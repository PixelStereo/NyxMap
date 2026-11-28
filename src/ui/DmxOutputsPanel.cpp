#include "DmxOutputsPanel.h"
#include "Commands.h"
#include "Engine.h"

#include <QApplication>
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

DmxOutputsPanel::DmxOutputsPanel(Engine *engine, QUndoStack *undo, QWidget *parent)
    : QGroupBox(QStringLiteral("DMX outputs"), parent), m_engine(engine), m_undo(undo)
{
    setObjectName(QStringLiteral("dmxOutputs"));
    auto *v = new QVBoxLayout(this);
    m_panel = new pxs::dmx::OutputsPanel;
    m_panel->setDefaultSourceName(QStringLiteral("NyxMap"));
    // Devices use this map. Pixel-mapped fixtures still specify the network universe directly.
    m_panel->setMappingShown(true);
    m_panel->setStateSource([this](quint64 id) { return m_engine->dmxOutputState(id); });
    v->addWidget(m_panel);

    connect(m_panel, &pxs::dmx::OutputsPanel::outputsChanged, this,
            [this](const QList<pxs::dmx::Output> &outputs, const QString &what, const QString &field) {
                const std::vector<DmxOutput> next(outputs.begin(), outputs.end());
                // The output that was edited (a run of edits of one setting of it is one undo step)
                quint64 edited = 0;
                const FixturePatch before = m_engine->fixturePatch();
                for (const DmxOutput &o : next) {
                    const auto old = std::find_if(before.outputs.begin(), before.outputs.end(), [&](const DmxOutput &x) { return x.id == o.id; });
                    if (old == before.outputs.end() || *old != o) {
                        edited = o.id;
                        break;
                    }
                }
                cmd::editFixtures(m_undo, m_engine, what, [next](FixturePatch &p) {
                    for (const DmxOutput &old : p.outputs) // an output deleted: its fixtures are left without one
                        if (std::none_of(next.begin(), next.end(), [&](const DmxOutput &x) { return x.id == old.id; }))
                            for (Fixture &f : p.fixtures)
                                if (f.output == old.id) f.output = 0;
                    p.outputs = next;
                }, field.isEmpty() ? QString() : QStringLiteral("output-%1-%2").arg(field).arg(edited));
                emit projectEdited();
            });

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(60);
    connect(m_refreshTimer, &QTimer::timeout, this, &DmxOutputsPanel::refresh);
    connect(m_engine, &Engine::fixturesChanged, m_refreshTimer, qOverload<>(&QTimer::start));
    refresh();
}

void DmxOutputsPanel::refresh()
{
    // Not under a rename in progress (a line edit in a cell has the focus): tried again a moment later
    if (const QWidget *f = QApplication::focusWidget(); f && m_panel->isAncestorOf(f) && qobject_cast<const QLineEdit *>(f)) {
        m_refreshTimer->start();
        return;
    }
    const FixturePatch p = m_engine->fixturePatch();
    m_panel->setOutputs(QList<pxs::dmx::Output>(p.outputs.begin(), p.outputs.end()));
}
