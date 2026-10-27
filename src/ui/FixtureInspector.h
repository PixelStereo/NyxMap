#pragma once
// The selected fixture's settings (Fixtures.h), as the Layer inspector is for a layer: its type and grid of pixels
// (for a type of one pixel), its output, universe and address, its place on the composition, its intensity, gamma and
// white. Every edit is undoable; a locked fixture keeps its place.

#include <QWidget>
#include <functional>

class Engine;
class IntBox;
class NumberBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QUndoStack;
class SliderField;
struct Fixture;
struct FixturePatch;

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
};
