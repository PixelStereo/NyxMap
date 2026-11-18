#pragma once
// The editing of a layer's masks (Masks.h), in any view: one class, whatever the view draws or shows. A view gives it the
// layer, how a point of a mask's frame lands on its widget (and back), and forwards its mouse and key events; the editor
// does the rest: picking a mask, dragging its points, its handles or the shape whole, adding a point (double-click on the
// contour), the point's menu (smooth, corner, delete), the arrows and Delete on the points picked, drawing a new mask
// (click for a point, drag for its curve, the first point, a double-click or Enter closes it, Esc cancels), the Layer /
// Mask button, and what to paint. Each change is one step of the undo stack.
//
// Used by the mapping view (the composition) and by the views of a layer or a viewport (PreviewWorkspace).

#include "Masks.h"
#include <QColor>
#include <QObject>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QSize>
#include <vector>

class Engine;
struct Layer;
class QContextMenuEvent;
class QKeyEvent;
class QMouseEvent;
class QToolButton;
class QUndoStack;
class QWidget;
class ZoomBar;

// What a view gives the editor. The engine's lock is held when the editor calls the conversions.
class MaskEditorHost
{
public:
    virtual ~MaskEditorHost() = default;
    virtual QWidget *maskWidget() = 0;
    virtual quint64 maskLayerId() const = 0; // the layer whose masks are edited (0: none)
    virtual bool maskEditable() const { return true; } // false: nothing to edit on (a layer with no picture here)
    // The masks shown here: a view of a layer's own picture shows the masks of that picture, not those of the composition
    virtual bool maskShown(const Mask &) const { return true; }
    // `composition`: the mask's frame is the composition (or its group's canvas), not the picture
    virtual bool maskInComposition(const Mask &) const { return false; }
    virtual QPointF maskToWidget(bool composition, QPointF frame) const = 0;
    virtual QPointF maskFromWidget(bool composition, QPointF widget, QPointF guess) const = 0;
    virtual QSize maskFrameSize(const Mask &mask) const = 0; // pixels of the mask's frame
    // Magnetism (the mapping view's): a point in widget coordinates caught by `others` (widget points), and, when `scene`,
    // by the rest of the composition too. Without it, the point as it is
    virtual bool maskSnapping(Qt::KeyboardModifiers) const { return false; }
    virtual QPointF maskSnap(QPointF widgetPoint, const std::vector<QPointF> &others, bool scene)
    {
        Q_UNUSED(others);
        Q_UNUSED(scene);
        return widgetPoint;
    }
    virtual void maskClearGuides() {}
};

// What to paint, in widget coordinates: segments (pairs of points), or squares around points
struct MaskPaint {
    enum class Kind { Lines, Squares } kind = Kind::Lines;
    QColor color;
    float half = 4; // squares: half the side
    std::vector<QPointF> points;
};

class MaskEditor : public QObject
{
    Q_OBJECT
public:
    MaskEditor(Engine *engine, MaskEditorHost *host, QObject *parent = nullptr);

    void setUndoStack(QUndoStack *undo) { m_undo = undo; }

    // The two tools: Layer (the view works on the layer) or Mask (the masks are edited). The button says which
    bool active() const { return m_active; }
    void setActive(bool on);
    QToolButton *makeButton(ZoomBar *bar, const QString &objectName); // the Layer / Mask button of the view's bar, kept in step

    int index() const { return m_index; } // the mask edited (-1: none)
    int node() const { return m_node; }   // its point last picked (-1: none)
    void setIndex(int index);             // picked elsewhere (the inspector's list): Mask mode too
    void reset();                         // another layer is shown
    bool drawing() const { return m_drawing; }
    void startDrawing();
    void cancelDrawing();

    // The view's events: true when the editor took the event
    bool mousePress(QMouseEvent *e);
    bool mouseMove(QMouseEvent *e);
    bool mouseRelease(QMouseEvent *e);
    bool mouseDoubleClick(QMouseEvent *e);
    bool keyPress(QKeyEvent *e);
    bool contextMenu(QContextMenuEvent *e);

    // The point picked, in pixels of its frame (the coordinate bar), and its move there
    bool nodePixels(QPointF *pixels, QString *name) const;
    bool moveNodeToPixels(QPointF pixels);

    std::vector<MaskPaint> paint() const;

signals:
    void masksEdited();       // a mask drawn, moved, edited or removed here
    void maskPicked(int index);
    void activeChanged(bool on);
    void changed();           // what is painted changed

private:
    enum class Drag { None, Node, In, Out, Body };
    Layer *layer() const; // lock held
    Mask *edited() const; // lock held
    QPolygonF outline(const Mask &mask) const;
    bool press(QMouseEvent *e);
    void finishDrawing();
    void commit(quint64 id, const std::vector<Mask> &before, const std::vector<Mask> &after, const QString &text);

    Engine *m_engine;
    MaskEditorHost *m_host;
    QUndoStack *m_undo = nullptr;
    QToolButton *m_button = nullptr;
    bool m_active = false;
    int m_index = -1, m_node = -1;
    std::vector<int> m_selection; // the points picked (Ctrl/⌘-click adds), moved together
    Drag m_drag = Drag::None;
    QPointF m_last;               // the frame point of the last move (a shape dragged whole)
    std::vector<Mask> m_before;   // the masks when a drag began
    bool m_drawing = false, m_pressing = false, m_tracking = false;
    std::vector<MaskNode> m_drawNodes;
    QPointF m_cursor;
};
