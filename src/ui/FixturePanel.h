#pragma once
// The pixel mapping's fixtures (Fixtures.h) as a list, like the layers: enable, lock, solo, name, intensity (a factor on
// what it sends) and its DMX address, address conflicts in red. Its other settings are in the Fixture inspector
// (FixtureInspector.h); the outputs in the Composition tab (DmxOutputsPanel.h). "+" adds a fixture of a type (GDTF
// files imported there too: Gdtf.h). Every edit is undoable.

#include "Fixtures.h"
#include <QWidget>
#include <functional>
#include <map>
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
struct FixtureType;

class FixturePanel : public QWidget
{
    Q_OBJECT
public:
    FixturePanel(Engine *engine, QUndoStack *undo, QWidget *parent = nullptr);
    void selectFixture(quint64 id); // 0: none
    quint64 selectedFixture() const;
    // A fixture of that type at the next free address of the first output (one made when there is none): its id
    quint64 addFixture(const QString &typeId);
    // A fixture of that type, its channels chosen first (pixel mapping or fixed, channels added): 0 if cancelled
    quint64 newFixture(const QString &typeId);
    quint64 newFixtureFrom(const FixtureType &type); // a template's or any type's: copied into the project with the fixture
    // A fixture of that type, its own channels these: the type copied into the project when it is not there (a template)
    quint64 addFixtureOfType(const FixtureType &type, const std::map<int, FixtureChannel> &own = {});
    // A type made from nothing (its name, pixels, channels), a fixture of it; kept as a template when asked
    void newFixtureType();
    // The channels of a type about to be imported, chosen in the channel editor: false to leave the type out
    bool reviewImported(FixtureType &type);
    // A strip of that type (one pixel, repeated `pixels` times) on each line of a Line generator layer, following it,
    // at the next free addresses (one undo step); the number made
    int addLineFixtures(quint64 layer, const QString &typeId, int pixels);
    // Each mode of each file a type of the project (a type of the same id replaced); the report: imported, warnings
    // review: each type read, changed or left out (false) before it is added
    QString importGdtf(const QStringList &paths, const std::function<bool(FixtureType &)> &review = {});
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
    quint64 selectedGroup() const;   // the group whose row is selected (or the fixture's group): 0 none
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
