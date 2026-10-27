#pragma once
// The DMX outputs of the pixel mapping (Fixtures.h, Dmx.h), in the Composition tab: a list (on, name, protocol, live
// state) and the selected output's settings — the network card it sends from (a menu of this machine's), broadcast
// or unicast to IP addresses (Art-Net), multicast or unicast (sACN), the sACN priority and source name, the rate.
// Only the settings of its protocol and mode are shown. Every edit is undoable.

#include <QGroupBox>
#include <functional>

class Engine;
class IntBox;
class QComboBox;
class QFormLayout;
class QLineEdit;
class QTableWidget;
class QTimer;
class QUndoStack;
struct DmxOutput;

class DmxOutputsPanel : public QGroupBox
{
    Q_OBJECT
public:
    DmxOutputsPanel(Engine *engine, QUndoStack *undo, QWidget *parent = nullptr);
    quint64 selectedOutput() const;

signals:
    void projectEdited();

private:
    void refresh();
    void refreshStates();
    void refreshEditor(); // the selected output's settings
    void editOutput(const QString &text, const QString &field, const std::function<void(DmxOutput &)> &fn);
    void setRowShown(QWidget *field, bool shown);

    Engine *m_engine;
    QUndoStack *m_undo;
    QTableWidget *m_table;
    QWidget *m_editor;
    QFormLayout *m_form;
    QComboBox *m_interface, *m_mode;
    QLineEdit *m_addresses, *m_sourceName;
    IntBox *m_priority, *m_rate;
    QTimer *m_refreshTimer, *m_stateTimer;
    bool m_updating = false;
};
