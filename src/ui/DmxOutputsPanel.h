#pragma once
// The DMX outputs of the pixel mapping (Fixtures.h), in the Composition tab: pxs's panel for a list of outputs (on, name, protocol,
// live state; the settings of the selected one: network card, broadcast or unicast, addresses, port, rate, sACN priority and source
// name, full or partial frames), with every edit undoable. Deleting an output leaves its fixtures without one.

#include <QGroupBox>
#include <pxs/OutputsPanel.h>

class Engine;
class QTimer;
class QUndoStack;

class DmxOutputsPanel : public QGroupBox
{
    Q_OBJECT
public:
    DmxOutputsPanel(Engine *engine, QUndoStack *undo, QWidget *parent = nullptr);

signals:
    void projectEdited();

private:
    void refresh();

    Engine *m_engine;
    QUndoStack *m_undo;
    pxs::dmx::OutputsPanel *m_panel;
    QTimer *m_refreshTimer;
};
