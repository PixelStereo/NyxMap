#pragma once
#include <QWidget>
#include <QHash>
#include <QPointer>
#include <functional>
#include <vector>
#include "ShowControl.h"

class Engine;
class QUndoStack;
class QVBoxLayout;
class QGridLayout;
class QToolButton;
class QLabel;
class QTimer;
class QStackedWidget;
class QShowEvent;

// The Controls tab: the operator's own sliders, switches, selectors, buttons and colors (ShowControl.h), each a parameter
// of the show that an interaction can follow (Internal source) or drive (target), a timeline animate and OSC reach. The
// panel is where they are made, shaped and played; the remote (RemoteServer.h) shows the same list. Definitions are
// edited through the undo stack; moving a control is a performance, not an edit.
class ControlsPanel : public QWidget
{
    Q_OBJECT
public:
    ControlsPanel(Engine *engine, QUndoStack *undo, QWidget *parent = nullptr);
    void openPage();
    void rebuild();      // the list again (a control added, removed, shaped)
    void refreshValues(); // the values as they are now (OSC, an interaction, a timeline, the remote), except one being dragged

signals:
    void edited(); // the project is modified

protected:
    void showEvent(QShowEvent *e) override; // the page is made when the panel is first seen (its server is up by then)

private:
    struct Row {
        quint64 id = 0;
        QPointer<QWidget> field; // SliderField, FlagBox, QComboBox, QPushButton or a swatch
    };
    void addControl(int kind);
    void fillContainer(QVBoxLayout *column, QGridLayout *grid, quint64 group, int columns, const std::vector<ShowControl> &all);
    QWidget *makeGroup(const ShowControl &c, const std::vector<const ShowControl *> &siblings, size_t position, const std::vector<ShowControl> &all);
    QWidget *makeRow(const ShowControl &c, const std::vector<const ShowControl *> &siblings, size_t position);
    QWidget *nameField(const ShowControl &c);
    QToolButton *gearButton(const ShowControl &c, const std::vector<const ShowControl *> &siblings, size_t position, const std::vector<ShowControl> &all);
    void applyControl(quint64 id, const ShowControl &change, const QString &text);
    void editControl(quint64 id);
    void editList(const QString &text, const std::function<void(QJsonArray &)> &change);
    void showPage(bool page); // the remote's page in a web view, or the list of rows
    void openWindow();         // the page in a window of its own, large enough to lay it out
    Engine *m_engine;
    QUndoStack *m_undo;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_web = nullptr;
    bool m_webStale = false; // the list changed while the page was hidden: it is loaded again when it is seen
    void refreshWeb();
    bool m_pageTried = false;
    QToolButton *m_pageButton = nullptr, *m_windowButton = nullptr;
    QVBoxLayout *m_list = nullptr;
    QLabel *m_empty = nullptr;
    QTimer *m_timer = nullptr;
    std::vector<Row> m_rows;
    quint64 m_revisionSeen = ~0ull;
    bool m_filling = false;
};
