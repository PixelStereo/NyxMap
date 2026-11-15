#pragma once
// The selected fixture's settings (Fixtures.h), as the Layer inspector is for a layer: its type and grid of pixels
// (for a type of one pixel), its output, universe and address, its place on the composition, its intensity, gamma and
// white. Every edit is undoable; a locked fixture keeps its place.

#include "Fixtures.h"
#include <QWidget>
#include <functional>
#include <optional>
#include <vector>

class Engine;
class IntBox;
class NumberBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QToolButton;
class QUndoStack;
class SliderField;

// `w` shows the show values at `paths` (a fixture's or a group's: "fixture/…"), as the inspector's numbers do: marked when
// in the selected preset or driven by an interaction or a timeline, Alt-click in Edit mode, and its right-click offers the
// preset, timeline and interaction actions. Called again: the same widget, other values (no second menu)
void attachShowValue(QWidget *w, Engine *e, QUndoStack *undo, const QStringList &paths);
// A type kept as a template: its own id (a project's type of the same id is not replaced), the name given
FixtureType templateOf(const FixtureType &type, const QString &name);

// A fixture's channels one by one: each follows the pixel mapping (its role: a colour, a dimmer) or sends a fixed value
// (0..255, as it is); channels can be added after its own. The same editor when a fixture is made, when a GDTF type is
// imported and in the Fixture inspector
class ChannelEditor : public QWidget
{
    Q_OBJECT
public:
    struct Row {
        int channel = 1;  // from the fixture's address (1: the address itself)
        int pixel = -1;   // -1: the whole fixture
        FixtureChannel now;
        std::optional<FixtureChannel> type; // its type's (none: a channel added)
    };
    explicit ChannelEditor(QWidget *parent = nullptr);
    void setRows(const std::vector<Row> &rows); // made again when the channels differ, else their values updated
    // The fixture the rows are of (the inspector's): its fixed values are show values ("fixture/<id>/chN"), marked and
    // with their right-click menu. None (0) in the dialogs, before the fixture exists
    void setOwner(Engine *e, QUndoStack *undo, quint64 fixture);
    const std::vector<Row> &rows() const { return m_rows; }

    static std::vector<Row> rowsOf(const FixtureType &t);                    // a type's channels, as its own
    static std::vector<Row> rowsOf(const FixturePatch &p, const Fixture &f); // a fixture's, over its type's
    static std::map<int, FixtureChannel> ownChannels(const std::vector<Row> &rows); // what differs from the type
    static FixtureType withRows(const FixtureType &t, const std::vector<Row> &rows); // the type, its channels these

signals:
    void edited(); // rows() changed here

private:
    void rebuild();
    void showValues();
    void change(int index, const FixtureChannel &c);
    QTableWidget *m_table;
    QToolButton *m_add;
    std::vector<Row> m_rows;
    bool m_updating = false;
    Engine *m_engine = nullptr;
    QUndoStack *m_undo = nullptr;
    quint64 m_fixture = 0;
    bool m_ownerChanged = false;
};

class FixtureInspector : public QWidget
{
    Q_OBJECT
public:
    FixtureInspector(Engine *engine, QUndoStack *undo, QWidget *parent = nullptr);
    void setFixture(quint64 id); // 0: none
    quint64 fixture() const { return m_id; }

signals:
    void projectEdited();

private:
    void refresh();
    // fn's change of this fixture as one step; edits of one field following one another merge
    void edit(const QString &text, const QString &field, const std::function<void(Fixture &, const FixturePatch &)> &fn);

    Engine *m_engine;
    QUndoStack *m_undo;
    quint64 m_id = 0;
    bool m_updating = false;
    QLabel *m_empty;
    QWidget *m_form;
    QLineEdit *m_name;
    QComboBox *m_type, *m_order, *m_start, *m_output, *m_white;
    IntBox *m_cols, *m_rows, *m_universe, *m_address;
    QCheckBox *m_snake;
    QLabel *m_typeInfo, *m_dmx;
    NumberBox *m_x, *m_y, *m_w, *m_h, *m_rotation, *m_gamma;
    SliderField *m_intensity;
    ChannelEditor *m_channels;
};
