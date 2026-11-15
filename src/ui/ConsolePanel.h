#pragma once
// The Console: every message of the application (what the status bar said, what Qt and the engine warned about), with
// filters (the level, the source, a text) and a command line below (for now: help, clear, filter; scripts later).
#include <QWidget>
#include <QTimer>
#include <vector>

#include "AppConsole.h"

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;
class QToolButton;
class ConsoleModel;

class ConsolePanel : public QWidget
{
    Q_OBJECT
public:
    explicit ConsolePanel(AppConsole *console = nullptr, QWidget *parent = nullptr);

    int shownCount() const; // the rows the filters let through
    // "Errors", "Warnings", "Info" or "All"; the source and text filters are cleared by "All"
    void setLevelFilter(const QString &name);
    void runCommand(const QString &line); // as typed in the command line

private:
    void refresh();
    void applyFilters();
    void say(AppConsole::Level level, const QString &text);
    AppConsole *m_console;
    ConsoleModel *m_model = nullptr;
    QTableView *m_view = nullptr;
    QButtonGroup *m_levels = nullptr;
    QToolButton *m_levelButtons[4] = {};
    QComboBox *m_source = nullptr;
    QLineEdit *m_search = nullptr, *m_command = nullptr;
    QLabel *m_summary = nullptr;
    QTimer m_timer;
    quint64 m_seen = ~quint64(0);
};
