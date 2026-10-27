#pragma once
// The pixel mapping's fixtures (Fixtures.h) as a list, like the layers: enable, lock, solo, name, intensity (a factor on
// what it sends) and its DMX address, address conflicts in red. Its other settings are in the Fixture inspector
// (FixtureInspector.h); the outputs in the Composition tab (DmxOutputsPanel.h). "+" adds a fixture of a type (GDTF
// files imported there too: Gdtf.h). Every edit is undoable.

#include <QWidget>
#include <vector>

class Engine;
class QLabel;
class QMenu;
class QTableWidget;
class QTimer;
class QToolButton;
class QUndoStack;

class FixturePanel : public QWidget
{
    Q_OBJECT
public:
    FixturePanel(Engine *engine, QUndoStack *undo, QWidget *parent = nullptr);
    void selectFixture(quint64 id); // 0: none
    quint64 selectedFixture() const;
    // A fixture of that type at the next free address of the first output (one made when there is none): its id
    quint64 addFixture(const QString &typeId);
    // Each mode of each file a type of the project (a type of the same id replaced); the report: imported, warnings
    QString importGdtf(const QStringList &paths);

signals:
    void fixtureSelected(quint64 id);
    void projectEdited();

private:
    void refresh();
    void fillAddMenu();
    void duplicateFixtures();
    void removeFixtures();
    std::vector<quint64> selectedIds() const;
    quint64 idAt(int row) const;

    Engine *m_engine;
    QUndoStack *m_undo;
    QTableWidget *m_table;
    QToolButton *m_add;
    QMenu *m_addMenu;
    QLabel *m_summary;
    QTimer *m_refreshTimer;
    bool m_updating = false;
};
