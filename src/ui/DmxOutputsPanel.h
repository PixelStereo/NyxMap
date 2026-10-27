#pragma once
// The DMX outputs of the pixel mapping (Fixtures.h, Dmx.h), in the Composition tab: protocol, destination, interface,
// rate, and the live state of each. Every edit is undoable.

#include <QGroupBox>

class Engine;
class QTableWidget;
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
    void refreshStates();
    void editCell(int row, int column);

    Engine *m_engine;
    QUndoStack *m_undo;
    QTableWidget *m_table;
    QTimer *m_refreshTimer, *m_stateTimer;
    bool m_updating = false;
};
