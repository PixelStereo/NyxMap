#pragma once
#include "Engine.h"
#include <QWidget>

class Engine;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QPlainTextEdit;
class QCheckBox;
class QToolButton;
class QTimer;
class QMenu;

// Where the snapshot recalled last is in its fade: a thin bar filling over its time (green while it runs), with the
// snapshot's name and the seconds as tooltip. Follows the engine by itself.
class RecallProgressBar : public QWidget
{
    Q_OBJECT
public:
    explicit RecallProgressBar(Engine *engine, QWidget *parent = nullptr);
    QSize sizeHint() const override { return QSize(100, 5); }
    QSize minimumSizeHint() const override { return QSize(10, 5); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void poll();
    Engine *m_engine;
    double m_fraction = 1;
    bool m_running = false;
};

// SequenceWindow: the panel of a sequence's steps, to edit everything: the sequences (new, duplicate, delete, rename,
// loop), their steps and groups of steps (+, delete, move, dragged in and out, a snapshot or a timeline dragged onto a
// step, a timeline step's action, its description), and to play any step. The step played and the one GO plays each have a thin frame.
class SequenceWindow : public QWidget
{
    Q_OBJECT
public:
    QString proposedTimecode(int row) const; // what an empty Timecode cell offers when edited
    void toggleNext(int row); // GO, or AUTO
    // A list row is a step, a step of an open group (action >= 0), or the row of a group played one GO at a time
    // (action == kHeaderRow, step: its first step)
    static constexpr int kHeaderRow = -2;
    int stepOfRow(int row) const { return row >= 0 && row < int(m_rows.size()) ? m_rows[size_t(row)].step : -1; }
    int actionOfRow(int row) const { return row >= 0 && row < int(m_rows.size()) ? m_rows[size_t(row)].action : -1; }
    int rowOf(int step, int action = -1) const;
    void toggleFold(int step); // a group's steps shown or not
    void toggleFolder(int row);
    void setGroupOneGoEach(int step);      // a group whose steps start together: played one GO at a time
    void setGroupTogether(int headerRow);  // and back
    void groupStepsOneGoEach();            // the steps selected, or a new one, as such a group
    // The rows taken to a place: before the step `top` (action < 0), or before the step `action` of the group `top`.
    // A step dragged, or moved by ▲ ▼, gets in and out of the groups this way
    // `folder`: the group played one GO at a time they go into, when action < 0
    void moveRowsTo(QList<int> rows, int top, int action, quint64 folder = 0);
    void dropPlace(int y, int *top, int *action, quint64 *folder = nullptr) const; // where a row dropped at y in the list goes
    void openTimeEditor(int row, bool post = false); // the step's pre-wait and post-wait, in a small window on its Time cell
    explicit SequenceWindow(Engine *engine, QWidget *parent = nullptr, class QUndoStack *undo = nullptr);
    void refresh();
    void setModified(bool on); // something changed since the current step was played
    void fillStepMenu(QMenu *menu, int row); // row -1: append a new step with the chosen target

signals:
    void goRequested();           // GO: the next step
    void backRequested();         // GO BACK
    void goToRequested(int step); // play that step of the current sequence
    void stopRequested();         // the pre-waits and follows still to come
    void stopAllRequested();      // those of every sequence, the ones playing in the background too
    void edited();                // the sequences changed: the project is modified

protected:
    bool eventFilter(QObject *o, QEvent *e) override; // snapshots dropped onto the steps

private:
    void refreshStepChanges();
    QPlainTextEdit *m_stepChanges = nullptr;
    void addStep(int at, quint64 snapshot, quint64 timeline = 0, quint64 preset = 0, quint64 sequence = 0, quint64 folder = 0);
    void setStepSnapshot(int row, quint64 snapshot);
    void setStepTimeline(int row, quint64 timeline); // Play, unless it was already a timeline step
    void setStepLibraryTarget(int row, quint64 preset, quint64 sequence);
    void fillActionMenu(QMenu *menu, int row);
    void simplifyStep(int row);
    void removeSteps();
    void applySequence(int index, const Sequencer::Sequence &sequence,
                       const QString &text = QStringLiteral("Edit Sequence"));
    QList<int> selectedSteps() const;
    void copySteps(bool cut = false);
    void pasteSteps();
    void duplicateSteps();
    Sequencer::Step *stepAt(Sequencer::Sequence &s, int row) const;
    Sequencer::Step *targetAt(Sequencer::Sequence &s, int row) const;
    void insertStep(int row, bool before);
    void groupSteps();
    void groupRange(QList<int> steps, bool leaveFolder = false);
    void ungroupStep();
    class QUndoStack *m_undo = nullptr;
    QCheckBox *m_editMode = nullptr;
    void moveStep(int delta);
    Engine *m_engine;
    QComboBox *m_sequences;
    quint64 m_shownSequence = 0; // the one whose steps are shown
    struct StepRow { int step, action; }; // action < 0: the step itself
    std::vector<StepRow> m_rows;
    std::vector<quint64> m_folders; // each step's
    class QFrame *m_dropLine = nullptr; // where a dragged row would go
    int m_followed = -1;         // the step the list last scrolled to, playing
    QCheckBox *m_loop, *m_beats;
    QCheckBox *m_tracking; // a jump plays first what the steps before it leave
    QToolButton *m_clock;  // the clock its steps' timecodes are on
    class NumberBox *m_bpm;
    QPushButton *m_tap, *m_clockRun;
    QLabel *m_beatLabel;
    QTableWidget *m_steps;
    QLabel *m_status; // the step played, and whether something changed since
    bool m_modified = false;
    QPushButton *m_add, *m_dup, *m_del, *m_stepAdd, *m_stepDel, *m_up, *m_down, *m_stop, *m_stopAll, *m_go, *m_back;
    bool m_filling = false;
    std::vector<Sequencer::Run> m_runs; // the steps on their way, drawn in their time columns
    int m_position = -1, m_upcoming = -1; // the step played (green frame) and the one GO plays (red frame)
    void pollRuns();
    void pollClock();
    void refreshTransport();
    friend class StepDelegate;
};
