#pragma once
// The pixel mapping's fixtures (Fixtures.h) as a list, like the layers: enable, lock, solo, name, intensity (a factor on
// what it sends) and its DMX address, address conflicts in red. Its other settings are in the Fixture inspector
// (FixtureInspector.h); the outputs in the Composition tab (DmxOutputsPanel.h). "+" adds a fixture of a type (GDTF
// files imported there too: Gdtf.h). Every edit is undoable.

#include <QWidget>
#include <functional>
#include <vector>

struct Fixture;
class Engine;
class QLabel;
class QMenu;
class QTableWidget;
class QTimer;
class QToolButton;
class QUndoStack;
class SliderField;

class FixturePanel : public QWidget
{
    Q_OBJECT
public:
    FixturePanel(Engine *engine, QUndoStack *undo, QWidget *parent = nullptr);
    void selectFixture(quint64 id); // 0: none
    quint64 selectedFixture() const;
    // A fixture of that type at the next free address of the first output (one made when there is none): its id
    quint64 addFixture(const QString &typeId);
    // A strip of that type (one pixel, repeated `pixels` times) on each line of a Line generator layer, following it,
    // at the next free addresses (one undo step); the number made
    int addLineFixtures(quint64 layer, const QString &typeId, int pixels);
    // Each mode of each file a type of the project (a type of the same id replaced); the report: imported, warnings
    QString importGdtf(const QStringList &paths);
    void duplicateFixtures();     // the selected ones, at the next free addresses
    void groupSelected();         // the selected fixtures in a new group (Cmd / Ctrl + G)
    bool hasFocusInside() const;  // the list has the keyboard

signals:
    void fixtureSelected(quint64 id);
    void projectEdited();

private:
    void refresh();
    void fillAddMenu();
    void removeFixtures();
    std::vector<quint64> selectedIds() const;
    quint64 groupAt(int row) const;   // a group's row: its group; 0 for a fixture's
    void editMembers(quint64 group, const QString &text, const std::function<void(Fixture &)> &change, bool lock = false, bool solo = false);
    void editIntensity(SliderField *slider, double value);
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
