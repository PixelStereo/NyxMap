#pragma once
// A 3D scene seen from the view's own camera (a preview cell showing a 3D scene, Model3D.h): turned around, panned and
// zoomed freely, its scene's camera and lights drawn in it, its models and lights picked and moved, turned or scaled
// along X, Y and Z with handles (W, E, R). The engine draws the scene from this camera (Engine::setPreviewScene); the
// grid, the camera, the lights and the handles are drawn over it here. Every gesture is one undo step.

#include "GlDraw.h"
#include "Model3D.h"
#include <QOpenGLWidget>
#include <QPointer>

class Engine;
class QUndoStack;
class QToolButton;
class QPainter;

class Scene3DView : public QOpenGLWidget
{
    Q_OBJECT
public:
    Scene3DView(Engine *engine, int slot, QWidget *parent = nullptr);
    ~Scene3DView() override;
    void setUndoStack(QUndoStack *undo) { m_undo = undo; }
    void setScene(quint64 scene);       // 0: none (the slot shows pictures again)
    void setSelected(quint64 layer);    // the layer selected in the list (its handles, if it is one of the scene's)
    quint64 scene() const { return m_scene; }
    enum class Mode { Move, Rotate, Scale };
    void setMode(Mode m);
    Mode mode() const { return m_mode; }
    const SceneSettings &camera() const { return m_cam; } // the view's camera
    void setCamera(const SceneSettings &c);
    void viewFromSceneCamera();  // the view takes the scene's camera
    void sceneCameraFromView();  // the scene's camera takes the view's (undoable)
    void frameSelection();       // turns around what is selected (the scene's middle when nothing is)
    QJsonObject viewState() const;
    void setViewState(const QJsonObject &o);
    QPointF project(const QVector3D &p, bool *front = nullptr) const; // a world point in the widget
    float handleLength() const;                                       // world: a handle's length here

signals:
    void layerPicked(int index); // a model, a light, or (its camera) the scene clicked here

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    struct Object {
        quint64 id = 0;
        bool light = false;
        int lightType = 1;
        QVector3D position, direction; // world; a light's direction (directional, spot)
        float radius = 0.1f;           // world: what a click picks
        QVector3D rotation, axisScale{1, 1, 1};
        float scale = 1;
    };
    std::vector<Object> objects() const; // the scene's shown models and lights (takes the lock)
    SceneSettings sceneCamera() const;
    Camera3D cameraNow() const;
    void pushCamera(); // the engine draws the scene from the view's camera, at the widget's size
    // A handle under the cursor: its axis (0 X, 1 Y, 2 Z, 3 all: the middle of the scale handles), -1 none
    int hitHandle(const Object &o, QPointF at) const;
    std::vector<QPointF> ring(const Object &o, int axis) const;
    void paintGround(QPainter &p);
    void paintOverlay(QPainter &p);
    void beginEdit(const Object &o);
    void commitEdit();
    QString pathOf(const Object &o, const QString &what, int axis) const; // "model/position/x"…

    Engine *m_engine;
    int m_slot;
    QUndoStack *m_undo = nullptr;
    GlDraw m_draw;
    quint64 m_scene = 0, m_selected = 0;
    SceneSettings m_cam;
    Mode m_mode = Mode::Move;
    QToolButton *m_modeButtons[3] = {};
    // Gesture
    enum class Drag { None, Orbit, Pan, Handle } m_drag = Drag::None;
    QPointF m_press, m_last;
    int m_axis = -1;
    Object m_start;                         // the object as the gesture began
    std::vector<std::pair<QString, double>> m_before; // its values then (undo)
    int m_hover = -1;
};
