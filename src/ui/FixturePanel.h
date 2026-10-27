#pragma once
// The pixel mapping's patch (Fixtures.h): the DMX outputs, the fixtures (type, output, universe, address, place on the
// composition, intensity) with their address conflicts, and the project's fixture types (GDTF files imported:
// Gdtf.h). Every edit is undoable.

#include <QWidget>

class Engine;
class QComboBox;
class QLabel;
class QListWidget;
class QTableWidget;
class QTimer;
class QUndoStack;

class FixturePanel : public QWidget
{
    Q_OBJECT
public:
    FixturePanel(Engine *engine, QUndoStack *undo, QWidget *parent = nullptr);
    void selectFixture(quint64 id); // 0: none
    quint64 selectedFixture() const;
    // Each mode of each file a type of the project (a type of the same id replaced); the report: imported, warnings
    QString importGdtf(const QStringList &paths);

signals:
    void fixtureSelected(quint64 id);
    void projectEdited();

private:
    void refresh();         // the tables from the engine's patch
    void refreshStates();   // the outputs' states
    void addFixture();
    void duplicateFixture();
    void removeFixtures();
    void addOutput();
    void removeOutput();
    void removeType();
    void editFixtureCell(int row, int column);
    void editOutputCell(int row, int column);

    Engine *m_engine;
    QUndoStack *m_undo;
    QTableWidget *m_outputs, *m_fixtures;
    QListWidget *m_types;
    QComboBox *m_newType;
    QLabel *m_summary;
    QTimer *m_refreshTimer, *m_stateTimer;
    bool m_updating = false;
};
