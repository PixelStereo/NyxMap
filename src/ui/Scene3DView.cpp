#include "Scene3DView.h"
#include "Commands.h"
#include "Engine.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QLocale>
#include <QPainter>
#include <functional>
#include <QPainterPath>
#include <QPaintEngine>
#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QToolButton>
#include <QUndoStack>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
const QColor kAxis[3] = {QColor(235, 80, 80), QColor(110, 210, 90), QColor(80, 140, 245)};
const QColor kLight(255, 214, 90), kCamera(150, 205, 255), kSelected(255, 170, 40), kMerge(160, 220, 170);
const char *kAxisName[3] = {"x", "y", "z"};

// The distance from a point to a segment (widget pixels)
double toSegment(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    const QPointF q = a + ab * t - p;
    return std::hypot(q.x(), q.y());
}
} // namespace

Scene3DView::Scene3DView(Engine *engine, int slot, QWidget *parent) : QOpenGLWidget(parent), m_engine(engine), m_slot(slot)
{
    setObjectName(QStringLiteral("scene3dView%1").arg(slot));
    setMinimumSize(100, 70);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    m_cam.distance = 4;
    m_cam.yaw = 35;
    m_cam.pitch = 25;
    m_cam.background = {{0, 0, 0, 0}}; // transparent: the ground drawn under it shows
    // Its bar: the handles' mode, the scene's camera
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("scene3dBar"));
    auto *rows = new QVBoxLayout(bar); // two rows: all of it in a narrow view too
    rows->setContentsMargins(6, 6, 6, 0);
    rows->setSpacing(3);
    auto *h = new QHBoxLayout;
    h->setSpacing(3);
    rows->addLayout(h);
    const QString names[3] = {QStringLiteral("Move"), QStringLiteral("Rotate"), QStringLiteral("Scale")};
    const QString keys[3] = {QStringLiteral("W"), QStringLiteral("E"), QStringLiteral("R")};
    for (int k = 0; k < 3; ++k) {
        auto *b = m_modeButtons[k] = new QToolButton;
        b->setText(names[k]);
        b->setObjectName(QStringLiteral("scene3d") + names[k]);
        b->setCheckable(true);
        b->setToolTip(QStringLiteral("%1 the selected model or light along X, Y, Z (%2)").arg(names[k], keys[k]));
        connect(b, &QToolButton::clicked, this, [this, k] { setMode(Mode(k)); });
        h->addWidget(b);
    }
    h->addSpacing(8);
    m_lookButton = new QToolButton;
    m_lookButton->setObjectName(QStringLiteral("scene3dLook"));
    m_lookButton->setPopupMode(QToolButton::InstantPopup);
    m_lookButton->setToolTip(QStringLiteral("How this view draws the models (the scene's own picture does not change)"));
    auto *looks = new QMenu(m_lookButton);
    auto *lookGroup = new QActionGroup(looks);
    const auto addLook = [&](const QString &name, const QString &tip, Scene3D::ViewLook which) {
        QAction *action = looks->addAction(name);
        action->setToolTip(tip);
        action->setCheckable(true);
        action->setData(int(which));
        lookGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, which] { setLook(which); });
    };
    addLook(QStringLiteral("Simple"), QStringLiteral("Flat colours and edges: the lightest"), Scene3D::ViewLook::Simple);
    addLook(QStringLiteral("Unlit"), QStringLiteral("The models' colours and pictures, no light on them"), Scene3D::ViewLook::Unlit);
    looks->setToolTipsVisible(true);
    m_lookButton->setMenu(looks);
    h->addWidget(m_lookButton);
    h->addStretch();
    auto *h2 = new QHBoxLayout;
    h2->setSpacing(3);
    rows->addLayout(h2);
    auto *look = new QToolButton;
    look->setText(QStringLiteral("View from Camera"));
    look->setObjectName(QStringLiteral("scene3dViewFromCamera"));
    look->setToolTip(QStringLiteral("This view takes the scene's camera (what the scene's picture shows)"));
    connect(look, &QToolButton::clicked, this, &Scene3DView::viewFromSceneCamera);
    h2->addWidget(look);
    auto *set = new QToolButton;
    set->setText(QStringLiteral("Camera = View"));
    set->setObjectName(QStringLiteral("scene3dCameraFromView"));
    set->setToolTip(QStringLiteral("The scene's camera takes this view (undoable): the scene's picture shows what is seen here"));
    connect(set, &QToolButton::clicked, this, &Scene3DView::sceneCameraFromView);
    h2->addWidget(set);
    auto *frame = new QToolButton;
    frame->setText(QStringLiteral("Frame"));
    frame->setToolTip(QStringLiteral("Turn around the selection (F)"));
    connect(frame, &QToolButton::clicked, this, &Scene3DView::frameSelection);
    h2->addWidget(frame);
    h2->addStretch();
    bar->setStyleSheet(QStringLiteral("QToolButton { background:rgba(30,30,34,200); padding:2px 6px; border-radius:3px; } "
                                      "QToolButton:checked { background:#0b5c69; color:white; } "
                                      "QToolButton#scene3dLook { padding-right:16px; }"));
    setLook(Scene3D::ViewLook::Simple);
    setMode(Mode::Move);
    setToolTip(QStringLiteral("Drag: turn around · Shift / middle button + drag: pan · wheel: closer, farther\n"
                              "Click a model or a light to select it; drag its handles (W move, E rotate, R scale) · "
                              "F: frame the selection"));
}

Scene3DView::~Scene3DView()
{
    if (context()) {
        makeCurrent();
        m_draw.destroy();
        doneCurrent();
    }
}

void Scene3DView::setScene(quint64 scene)
{
    m_scene = scene;
    if (scene) pushCamera();
    else m_engine->setPreviewScene(m_slot, false);
    update();
}

void Scene3DView::setSelected(quint64 layer)
{
    m_selected = layer;
    update();
}

void Scene3DView::setMode(Mode m)
{
    m_mode = m;
    for (int k = 0; k < 3; ++k) m_modeButtons[k]->setChecked(int(m) == k);
    update();
}

void Scene3DView::setLook(Scene3D::ViewLook look)
{
    m_look = look;
    for (QAction *action : m_lookButton->menu()->actions())
        if (action->data().toInt() == int(look)) {
            action->setChecked(true);
            m_lookButton->setText(action->text());
        }
    if (auto *bar = findChild<QWidget *>(QStringLiteral("scene3dBar"))) bar->adjustSize();
    pushCamera();
    update();
}

void Scene3DView::setCamera(const SceneSettings &c)
{
    m_cam.target = c.target;
    m_cam.distance = std::clamp(c.distance, 0.05f, 500.0f);
    m_cam.yaw = c.yaw;
    m_cam.pitch = std::clamp(c.pitch, -89.0f, 89.0f);
    m_cam.fov = c.fov;
    pushCamera();
    update();
}

void Scene3DView::pushCamera()
{
    if (!m_scene) return;
    const qreal dpr = devicePixelRatioF();
    m_engine->setPreviewScene(m_slot, true, m_cam, QSize(int(width() * dpr), int(height() * dpr)), m_look);
}

SceneSettings Scene3DView::sceneCamera() const
{
    Engine::Lock lk(&m_engine->mutex());
    const Layer *s = m_engine->layer(m_engine->indexOfId(m_scene));
    return s ? s->scene : SceneSettings();
}

void Scene3DView::viewFromSceneCamera() { setCamera(sceneCamera()); }

void Scene3DView::sceneCameraFromView()
{
    if (!m_scene || m_engine->isLocked(m_engine->indexOfId(m_scene))) return;
    std::vector<cmd::ValueChange> changes;
    auto change = [&](const QString &path, double v) {
        changes.push_back({m_scene, path, m_engine->parameterValue(m_scene, path), v});
    };
    change(QStringLiteral("camera/distance"), m_cam.distance);
    change(QStringLiteral("camera/yaw"), m_cam.yaw);
    change(QStringLiteral("camera/pitch"), m_cam.pitch);
    change(QStringLiteral("camera/fov"), m_cam.fov);
    for (int a = 0; a < 3; ++a) change(QStringLiteral("camera/target/") + kAxisName[a], m_cam.target[size_t(a)]);
    if (m_undo) m_undo->push(new cmd::SetValues(m_engine, changes, QStringLiteral("Camera from 3D View")));
    else
        for (const auto &c : changes) m_engine->setParameter(m_scene, c.path, c.after);
    update();
}

void Scene3DView::frameSelection()
{
    for (const Object &o : objects())
        if (o.id == m_selected) {
            m_cam.target = {{o.position.x(), o.position.y(), o.position.z()}};
            m_cam.distance = std::max(1.0f, o.radius * 6);
            pushCamera();
            update();
            return;
        }
    m_cam.target = {{0, 0, 0}};
    m_cam.distance = 4;
    pushCamera();
    update();
}

QJsonObject Scene3DView::viewState() const
{
    return {{"target", QJsonArray{m_cam.target[0], m_cam.target[1], m_cam.target[2]}},
            {"distance", m_cam.distance}, {"yaw", m_cam.yaw}, {"pitch", m_cam.pitch}, {"fov", m_cam.fov},
            {"look", m_look == Scene3D::ViewLook::Unlit ? "unlit" : "simple"}};
}

void Scene3DView::setViewState(const QJsonObject &o)
{
    if (o.isEmpty()) return;
    SceneSettings c = m_cam;
    const QJsonArray t = o.value("target").toArray();
    if (t.size() == 3)
        for (int a = 0; a < 3; ++a) c.target[size_t(a)] = float(t[a].toDouble());
    c.distance = float(o.value("distance").toDouble(c.distance));
    c.yaw = float(o.value("yaw").toDouble(c.yaw));
    c.pitch = float(o.value("pitch").toDouble(c.pitch));
    c.fov = float(o.value("fov").toDouble(c.fov));
    setCamera(c);
    setLook(o.value("look").toString() == "unlit" ? Scene3D::ViewLook::Unlit : Scene3D::ViewLook::Simple);
}

QVector3D Scene3DView::axisOf(const Object &o, int axis)
{
    QVector3D unit;
    unit[axis] = 1;
    return o.parent.mapVector(unit).normalized();
}

std::vector<Scene3DView::Object> Scene3DView::objects() const
{
    std::vector<Object> out;
    Engine::Lock lk(&m_engine->mutex());
    // Those of a scene or a Merge 3D, then what each Merge 3D holds, under its transform
    const std::function<void(quint64, const QMatrix4x4 &, quint64)> collect = [&](quint64 holder, const QMatrix4x4 &parent, quint64 parentId) {
        for (int i = 0; i < m_engine->layerCount(); ++i) {
            const Layer *l = m_engine->layer(i);
            if (!l || l->parent != holder || !l->is3dObject()) continue;
            Object o;
            o.id = l->id;
            o.parent = parent;
            o.parentId = parentId;
            const ModelPlacement &p = l->modelPlacement;
            if (l->merge3d) {
                o.merge = true;
                o.position = parent.map(QVector3D(p.position[0], p.position[1], p.position[2]));
                o.rotation = QVector3D(p.rotation[0], p.rotation[1], p.rotation[2]);
                o.axisScale = QVector3D(p.axisScale[0], p.axisScale[1], p.axisScale[2]);
                o.scale = p.scale;
                o.radius = 0.1f;
                out.push_back(o);
                if (l->enabled) collect(l->id, parent * p.transform(), l->id);
                continue;
            }
            if (l->type == SourceType::Light) {
                o.light = true;
                o.lightType = l->light.type;
                o.angle = l->light.angle;
                o.falloff = l->light.falloff;
                const LightSettings placed = m_engine->placedLight(*l, parent, &o.lookingAt);
                o.looks = placed.lookAt;
                o.position = QVector3D(placed.position[0], placed.position[1], placed.position[2]);
                o.direction = Camera3D::aim(placed.yaw, placed.pitch);
                for (const QVector3D &point : l->light.pathOutline()) o.path.push_back(parent.map(point));
                o.rotation = QVector3D(l->light.pitch, l->light.yaw, 0);
                o.radius = 0.15f;
            } else {
                o.position = parent.map(QVector3D(p.position[0], p.position[1], p.position[2]));
                o.rotation = QVector3D(p.rotation[0], p.rotation[1], p.rotation[2]);
                o.axisScale = QVector3D(p.axisScale[0], p.axisScale[1], p.axisScale[2]);
                o.scale = p.scale;
                if (l->model) { // its bounds, placed: what a click picks
                    const QMatrix4x4 m = parent * l->model->matrix(p);
                    const QVector3D c = m.map(l->model->center());
                    o.position = c;
                    o.radius = std::max(0.05f, (m.map(l->model->center() + QVector3D(l->model->radius(), 0, 0)) - c).length());
                }
            }
            out.push_back(o);
        }
    };
    collect(m_scene, QMatrix4x4(), 0);
    return out;
}

Camera3D Scene3DView::cameraNow() const { return Camera3D::of(m_cam, float(width()) / float(std::max(1, height()))); }

QPointF Scene3DView::project(const QVector3D &p, bool *front) const
{
    const Camera3D c = cameraNow();
    const QVector4D clip = c.proj * c.view * QVector4D(p, 1);
    if (front) *front = clip.w() > 1e-4f;
    const float w = std::abs(clip.w()) > 1e-6f ? clip.w() : 1e-6f;
    return QPointF((clip.x() / w + 1) * 0.5 * width(), (1 - clip.y() / w) * 0.5 * height());
}

float Scene3DView::handleLength() const { return std::max(0.05f, m_cam.distance * 0.18f); }

std::vector<QPointF> Scene3DView::ring(const Object &o, int axis) const
{
    std::vector<QPointF> pts;
    const float r = handleLength();
    for (int k = 0; k <= 48; ++k) {
        const float a = float(2 * M_PI * k / 48);
        QVector3D d;
        if (axis == 0) d = QVector3D(0, std::cos(a), std::sin(a));
        else if (axis == 1) d = QVector3D(std::cos(a), 0, std::sin(a));
        else d = QVector3D(std::cos(a), std::sin(a), 0);
        pts.push_back(project(o.position + o.parent.mapVector(d).normalized() * r));
    }
    return pts;
}

int Scene3DView::hitHandle(const Object &o, QPointF at) const
{
    const QPointF c = project(o.position);
    if (m_mode == Mode::Rotate) {
        int best = -1;
        double bestD = 7;
        const int axes = o.light ? 2 : 3; // a light turns on its yaw (Y) and pitch (X)
        for (int a = 0; a < axes; ++a) {
            const auto pts = ring(o, a);
            for (size_t k = 0; k + 1 < pts.size(); ++k)
                if (const double d = toSegment(at, pts[k], pts[k + 1]); d < bestD) bestD = d, best = a;
        }
        return best;
    }
    if (m_mode == Mode::Scale && !o.light && std::hypot(at.x() - c.x(), at.y() - c.y()) < 8) return 3; // all of it
    int best = -1;
    double bestD = 8;
    for (int a = 0; a < 3; ++a) {
        const double d = toSegment(at, c, project(o.position + axisOf(o, a) * handleLength()));
        if (d < bestD) bestD = d, best = a;
    }
    return best;
}

QString Scene3DView::pathOf(const Object &o, const QString &what, int axis) const
{
    if (o.light) {
        if (what == QLatin1String("rotation")) return axis == 1 ? QStringLiteral("light/yaw") : QStringLiteral("light/pitch");
        return QStringLiteral("light/position/") + kAxisName[axis];
    }
    const QString base = o.merge ? QStringLiteral("merge") : QStringLiteral("model");
    if (what == QLatin1String("scale") && axis == 3) return base + QStringLiteral("/scale/all");
    return QStringLiteral("%1/%2/%3").arg(base, what, kAxisName[axis]);
}

void Scene3DView::beginEdit(const Object &o)
{
    m_start = o;
    m_before.clear();
    QStringList paths;
    for (int a = 0; a < 3; ++a) {
        paths << pathOf(o, QStringLiteral("position"), a);
        if (!o.light) paths << pathOf(o, QStringLiteral("rotation"), a) << pathOf(o, QStringLiteral("scale"), a);
    }
    if (o.light) paths << QStringLiteral("light/yaw") << QStringLiteral("light/pitch");
    else paths << pathOf(o, QStringLiteral("scale"), 3);
    for (const QString &p : paths) m_before.push_back({p, m_engine->parameterValue(o.id, p).toDouble()});
}

void Scene3DView::commitEdit()
{
    std::vector<cmd::ValueChange> changes;
    for (const auto &[path, before] : m_before) {
        const QJsonValue now = m_engine->parameterValue(m_start.id, path);
        if (std::abs(now.toDouble() - before) > 1e-9) changes.push_back({m_start.id, path, before, now});
    }
    m_before.clear();
    if (changes.empty()) return;
    const QString text = m_mode == Mode::Move ? QStringLiteral("Move in 3D") : m_mode == Mode::Rotate ? QStringLiteral("Rotate in 3D")
                                                                                                      : QStringLiteral("Scale in 3D");
    if (m_undo) m_undo->push(new cmd::SetValues(m_engine, changes, text));
}

void Scene3DView::initializeGL() { m_draw.init(); }

void Scene3DView::resizeEvent(QResizeEvent *e)
{
    QOpenGLWidget::resizeEvent(e);
    pushCamera();
}

void Scene3DView::paintGL()
{
    QPainter p(this); // the ground, the engine's picture of the scene over it (native GL), then the rest
    p.beginNativePainting();
    auto *f = context()->extraFunctions();
    const qreal dpr = devicePixelRatioF();
    f->glViewport(0, 0, int(width() * dpr), int(height() * dpr));
    f->glClearColor(0.11f, 0.11f, 0.12f, 1);
    f->glClear(GL_COLOR_BUFFER_BIT);
    p.endNativePainting();
    paintGround(p);
    p.beginNativePainting();
    f->glViewport(0, 0, int(width() * dpr), int(height() * dpr));
    {
        Engine::Lock lock(&m_engine->mutex());
        m_engine->waitForFrame(f);
        QSize size;
        int buffer = 0;
        const GLuint texture = m_engine->previewTexture(m_slot, &size, &buffer);
        if (texture && !size.isEmpty()) {
            m_draw.drawTexture(texture, true);
            m_engine->finishPreviewRead(m_slot, buffer, f);
        }
    }
    p.endNativePainting();
    paintOverlay(p);
}

// The ground: a grid on X Z and its axes, under the scene
void Scene3DView::paintGround(QPainter &p)
{
    p.setRenderHint(QPainter::Antialiasing);
    for (int k = -5; k <= 5; ++k) {
        bool f1, f2, f3, f4;
        const QPointF a = project({float(k), 0, -5}, &f1), b = project({float(k), 0, 5}, &f2);
        const QPointF c = project({-5, 0, float(k)}, &f3), d = project({5, 0, float(k)}, &f4);
        p.setPen(QPen(k == 0 ? QColor(80, 140, 245, 140) : QColor(255, 255, 255, 30), 1));
        if (f1 && f2) p.drawLine(a, b);
        p.setPen(QPen(k == 0 ? QColor(235, 80, 80, 140) : QColor(255, 255, 255, 30), 1));
        if (f3 && f4) p.drawLine(c, d);
    }
}

void Scene3DView::paintOverlay(QPainter &p)
{
    p.setRenderHint(QPainter::Antialiasing);
    // The scene's camera: its frustum toward its target
    {
        const SceneSettings s = sceneCamera();
        const QSize comp = m_engine->compositionSize();
        const float aspect = float(comp.width()) / float(std::max(1, comp.height()));
        const QVector3D target(s.target[0], s.target[1], s.target[2]);
        const QVector3D eye = target + Camera3D::aim(s.yaw, s.pitch) * std::max(0.01f, s.distance);
        const QVector3D fwd = (target - eye).normalized(), right = QVector3D::crossProduct(fwd, {0, 1, 0}).normalized(),
                        up = QVector3D::crossProduct(right, fwd);
        const float d = std::min(0.6f, s.distance * 0.25f), hh = std::tan(qDegreesToRadians(s.fov) / 2) * d, hw = hh * aspect;
        QVector3D corner[4] = {eye + fwd * d - right * hw + up * hh, eye + fwd * d + right * hw + up * hh,
                               eye + fwd * d + right * hw - up * hh, eye + fwd * d - right * hw - up * hh};
        bool fe;
        const QPointF e2 = project(eye, &fe);
        if (fe) {
            p.setPen(QPen(kCamera, 1.4));
            QPointF c2[4];
            for (int k = 0; k < 4; ++k) c2[k] = project(corner[k]);
            for (int k = 0; k < 4; ++k) {
                p.drawLine(e2, c2[k]);
                p.drawLine(c2[k], c2[(k + 1) % 4]);
            }
            p.setPen(QPen(kCamera, 1, Qt::DashLine));
            p.drawLine(e2, project(target));
            p.setPen(kCamera);
            p.drawText(e2 + QPointF(8, -6), QStringLiteral("Camera"));
        }
    }
    const std::vector<Object> objs = objects();
    // The Merge 3Ds: a diamond, and a line to each thing it holds
    for (const Object &o : objs) {
        if (!o.merge && !o.parentId) continue;
        if (o.parentId) {
            for (const Object &up : objs) {
                if (up.id != o.parentId) continue;
                bool a, b;
                const QPointF from = project(up.position, &a), to = project(o.position, &b);
                if (a && b) {
                    p.setPen(QPen(kMerge, 1, Qt::DotLine));
                    p.drawLine(from, to);
                }
            }
        }
        if (o.merge) {
            bool front;
            const QPointF c = project(o.position, &front);
            if (!front) continue;
            const bool sel = o.id == m_selected;
            QPolygonF diamond;
            diamond << c + QPointF(0, -8) << c + QPointF(8, 0) << c + QPointF(0, 8) << c + QPointF(-8, 0);
            p.setPen(QPen(sel ? kSelected : kMerge, sel ? 2 : 1.4));
            p.setBrush(QColor(kMerge.red(), kMerge.green(), kMerge.blue(), 70));
            p.drawPolygon(diamond);
        }
    }
    // The paths the lights go around, and what they look at
    for (const Object &o : objs) {
        if (!o.light) continue;
        const QColor ink = o.id == m_selected ? kSelected : kLight;
        const auto line = [&](const QVector3D &a, const QVector3D &b) {
            bool frontA, frontB;
            const QPointF pa = project(a, &frontA), pb = project(b, &frontB);
            if (frontA && frontB) p.drawLine(pa, pb);
        };
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(ink, o.id == m_selected ? 1.6 : 1));
        for (size_t k = 1; k < o.path.size(); ++k) line(o.path[k - 1], o.path[k]);
        if (o.looks) {
            p.setPen(QPen(ink, 1, Qt::DashLine));
            line(o.position, o.lookingAt);
        }
    }
    // The lights: where they are, where a directional light or a spot points
    for (const Object &o : objs) {
        if (!o.light) continue;
        bool front;
        const QPointF c = project(o.position, &front);
        if (!front) continue;
        const bool sel = o.id == m_selected;
        const QColor ink = sel ? kSelected : kLight;
        const QColor faint(ink.red(), ink.green(), ink.blue(), sel ? 170 : 110);
        // Two directions across where it points
        const QVector3D across = QVector3D::crossProduct(o.direction, std::abs(o.direction.y()) > 0.95f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0)).normalized();
        const QVector3D across2 = QVector3D::crossProduct(o.direction, across).normalized();
        p.setBrush(Qt::NoBrush);
        if (o.lightType == 2) {
            // Its cone, down to the ground when it reaches it: what it lights, and inside it where its falloff starts
            float reach = handleLength() * 3;
            if (o.direction.y() < -0.05f && o.position.y() > 0) reach = std::min(o.position.y() / -o.direction.y(), std::max(reach, m_cam.distance * 3));
            const auto ring = [&](float degrees) {
                const float spread = reach * std::tan(qDegreesToRadians(std::clamp(degrees, 0.0f, 85.0f)));
                QPolygonF poly;
                bool whole = true;
                for (int k = 0; k <= 48; ++k) {
                    const double t = k * 2 * M_PI / 48;
                    bool ahead;
                    poly << project(o.position + o.direction * reach + (across * float(std::cos(t)) + across2 * float(std::sin(t))) * spread, &ahead);
                    whole = whole && ahead;
                }
                return whole ? poly : QPolygonF();
            };
            const QPolygonF edge = ring(o.angle), inner = ring(o.angle * (1 - std::clamp(o.falloff, 0.0f, 1.0f)));
            if (!edge.isEmpty()) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(ink.red(), ink.green(), ink.blue(), sel ? 36 : 20));
                p.drawPolygon(edge);
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(faint, sel ? 1.6 : 1.1));
                p.drawPolygon(edge);
                for (int k = 0; k < 8; ++k) p.drawLine(c, edge[k * 6]);
                if (!inner.isEmpty() && o.falloff > 0.01f) {
                    p.setPen(QPen(faint, 1, Qt::DashLine));
                    p.drawPolygon(inner);
                }
            }
            // Its symbol: a lamp's head, wide towards where it points
            bool ahead;
            const QPointF tip = project(o.position + o.direction * handleLength() * 0.5f, &ahead);
            QPointF way = tip - c;
            const double length = std::hypot(way.x(), way.y());
            way = length > 1 ? way / length : QPointF(0, 1);
            const QPointF side(-way.y(), way.x());
            QPolygonF head;
            head << c - way * 5 - side * 3 << c - way * 5 + side * 3 << c + way * 7 + side * 8 << c + way * 7 - side * 8;
            p.setPen(QPen(ink, sel ? 2 : 1.4));
            p.setBrush(QColor(ink.red(), ink.green(), ink.blue(), 110));
            p.drawPolygon(head);
        } else if (o.lightType == 0) {
            // The sun: a disc, and parallel rays where it shines
            p.setPen(QPen(ink, sel ? 2 : 1.4));
            p.setBrush(QColor(ink.red(), ink.green(), ink.blue(), 110));
            p.drawEllipse(c, 6, 6);
            p.setPen(QPen(faint, sel ? 1.6 : 1.2));
            const float gap = handleLength() * 0.45f, length = handleLength() * 2;
            for (int k = 0; k < 5; ++k) {
                const QVector3D from = o.position + (k == 4 ? QVector3D() : (k % 2 ? across2 : across) * (k < 2 ? gap : -gap));
                bool a1, a2;
                const QPointF start = project(from, &a1), end = project(from + o.direction * length, &a2);
                if (!a1 || !a2) continue;
                p.drawLine(start, end);
                QPointF way = end - start;
                const double size = std::hypot(way.x(), way.y());
                if (size < 1) continue;
                way /= size;
                const QPointF side(-way.y(), way.x());
                p.drawLine(end, end - way * 7 + side * 4);
                p.drawLine(end, end - way * 7 - side * 4);
            }
        } else {
            // A bulb: rays all around
            p.setPen(QPen(ink, sel ? 2 : 1.4));
            p.setBrush(QColor(ink.red(), ink.green(), ink.blue(), 60));
            p.drawEllipse(c, 7, 7);
            for (int k = 0; k < 8; ++k) {
                const double a = k * M_PI / 4;
                p.drawLine(c + QPointF(std::cos(a), std::sin(a)) * 9, c + QPointF(std::cos(a), std::sin(a)) * 13);
            }
        }
    }
    // The selected object's outline and handles
    for (const Object &o : objs) {
        if (o.id != m_selected) continue;
        bool front;
        const QPointF c = project(o.position, &front);
        if (!front) break;
        if (!o.light && !o.merge) { // its bounds, as a circle
            const QPointF edge = project(o.position + cameraNow().view.inverted().mapVector({1, 0, 0}).normalized() * o.radius);
            p.setPen(QPen(kSelected, 1, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            const double r = std::hypot(edge.x() - c.x(), edge.y() - c.y());
            p.drawEllipse(c, r, r);
        }
        const int active = m_drag == Drag::Handle ? m_axis : m_hover;
        if (m_mode == Mode::Rotate) {
            for (int a = 0; a < (o.light ? 2 : 3); ++a) {
                const auto pts = ring(o, a);
                p.setPen(QPen(kAxis[a], a == active ? 3 : 1.6));
                for (size_t k = 0; k + 1 < pts.size(); ++k) p.drawLine(pts[k], pts[k + 1]);
            }
        } else {
            for (int a = 0; a < 3; ++a) {
                const QPointF e = project(o.position + axisOf(o, a) * handleLength());
                p.setPen(QPen(kAxis[a], a == active ? 3.5 : 2));
                p.drawLine(c, e);
                p.setBrush(kAxis[a]);
                if (m_mode == Mode::Scale && !o.light) p.drawRect(QRectF(e - QPointF(4, 4), QSizeF(8, 8)));
                else p.drawEllipse(e, 4, 4);
            }
            if (m_mode == Mode::Scale && !o.light) {
                p.setPen(QPen(Qt::white, active == 3 ? 2.5 : 1.2));
                p.setBrush(QColor(255, 255, 255, 60));
                p.drawRect(QRectF(c - QPointF(6, 6), QSizeF(12, 12)));
            }
        }
        break;
    }
    if (m_scene) { // what it costs, in a corner
        const int scene = m_engine->indexOfId(m_scene);
        const QLocale here;
        p.setPen(QColor(170, 170, 176));
        p.drawText(rect().adjusted(10, 0, -10, -8), Qt::AlignLeft | Qt::AlignBottom,
                   QStringLiteral("%1 triangles").arg(here.toString(m_engine->sceneTriangles(scene))));
    }
    if (!m_scene) {
        p.setPen(QColor(150, 150, 155));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No 3D scene"));
    }
}

void Scene3DView::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    m_press = m_last = e->position();
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier))) {
        m_drag = Drag::Pan;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    const std::vector<Object> objs = objects();
    // A handle of the selected object first
    for (const Object &o : objs)
        if (o.id == m_selected && !m_engine->isLocked(m_engine->indexOfId(o.id))) {
            const int axis = hitHandle(o, e->position());
            if (axis >= 0) {
                m_axis = axis;
                m_drag = Drag::Handle;
                beginEdit(o);
                return;
            }
        }
    // A light, then a model, under the cursor: the nearest one
    const QVector3D eye = cameraNow().eye;
    quint64 picked = 0;
    float nearest = std::numeric_limits<float>::max();
    for (const Object &o : objs) {
        bool front;
        const QPointF c = project(o.position, &front);
        if (!front) continue;
        double r = 10;
        if (!o.light && !o.merge) {
            const QPointF edge = project(o.position + cameraNow().view.inverted().mapVector({1, 0, 0}).normalized() * o.radius);
            r = std::max(8.0, std::hypot(edge.x() - c.x(), edge.y() - c.y()));
        }
        const float dist = (o.position - eye).length() - (o.light || o.merge ? 1000 : 0); // a light, a Merge 3D over a model
        if (std::hypot(e->position().x() - c.x(), e->position().y() - c.y()) <= r && dist < nearest) {
            nearest = dist;
            picked = o.id;
        }
    }
    if (picked) {
        m_selected = picked;
        emit layerPicked(m_engine->indexOfId(picked));
        update();
        return;
    }
    m_drag = Drag::Orbit;
}

void Scene3DView::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF at = e->position(), delta = at - m_last;
    m_last = at;
    if (m_drag == Drag::None) { // the handle under the cursor, highlighted
        int hover = -1;
        for (const Object &o : objects())
            if (o.id == m_selected) hover = hitHandle(o, at);
        if (hover != m_hover) {
            m_hover = hover;
            update();
        }
        return;
    }
    if (m_drag == Drag::Orbit) {
        m_cam.yaw = float(std::remainder(m_cam.yaw - delta.x() * 0.4, 360.0));
        m_cam.pitch = std::clamp(m_cam.pitch + float(delta.y()) * 0.4f, -89.0f, 89.0f);
    } else if (m_drag == Drag::Pan) {
        const Camera3D c = cameraNow();
        const QMatrix4x4 inv = c.view.inverted();
        const float k = m_cam.distance * 2 * std::tan(qDegreesToRadians(m_cam.fov) / 2) / float(std::max(1, height()));
        const QVector3D move = inv.mapVector({1, 0, 0}) * float(-delta.x()) * k + inv.mapVector({0, 1, 0}) * float(delta.y()) * k;
        for (int a = 0; a < 3; ++a) m_cam.target[size_t(a)] += move[a];
    } else if (m_drag == Drag::Handle) {
        const Object &o = m_start;
        const QPointF c = project(o.position), total = at - m_press;
        if (m_mode == Mode::Rotate) {
            const QPointF a0 = m_press - c, a1 = at - c;
            double turn = qRadiansToDegrees(std::atan2(-a1.y(), a1.x()) - std::atan2(-a0.y(), a0.x()));
            turn = std::remainder(turn, 360.0);
            const QVector3D axis = axisOf(o, m_axis);
            const float toward = QVector3D::dotProduct(axis, cameraNow().eye - o.position) >= 0 ? 1 : -1;
            const double base = o.light ? (m_axis == 1 ? o.rotation.y() : o.rotation.x()) : o.rotation[m_axis];
            m_engine->setParameter(o.id, pathOf(o, QStringLiteral("rotation"), m_axis), base + turn * toward);
        } else {
            const int axisIndex = std::min(m_axis, 2);
            const QVector3D axis = axisOf(o, axisIndex);
            QPointF screen = project(o.position + axis * handleLength()) - c;
            if (m_axis == 3) screen = QPointF(1, -1) * 40; // all of it: up and to the right grows it
            const double len2 = QPointF::dotProduct(screen, screen);
            const double amount = len2 > 1 ? QPointF::dotProduct(total, screen) / len2 : 0; // in handle lengths
            if (m_mode == Mode::Move) {
                QVector3D start;
                for (const auto &[path, v] : m_before)
                    for (int a = 0; a < 3; ++a)
                        if (path == pathOf(o, QStringLiteral("position"), a)) start[a] = float(v);
                // Its value is in its Merge 3Ds' space: a handle's length in the scene is less there when they are large
                QVector3D unit;
                unit[axisIndex] = 1;
                const float across = std::max(1e-4f, o.parent.mapVector(unit).length());
                m_engine->setParameter(o.id, pathOf(o, QStringLiteral("position"), axisIndex),
                                       start[axisIndex] + amount * handleLength() / across);
            } else { // scale: a handle's length dragged doubles it
                const double factor = std::max(0.01, 1 + amount);
                const double base = m_axis == 3 ? o.scale : o.axisScale[axisIndex];
                m_engine->setParameter(o.id, pathOf(o, QStringLiteral("scale"), m_axis), base * factor);
            }
        }
    }
    pushCamera();
    update();
}

void Scene3DView::mouseReleaseEvent(QMouseEvent *)
{
    if (m_drag == Drag::Handle) commitEdit();
    m_drag = Drag::None;
    m_axis = -1;
    update();
}

void Scene3DView::mouseDoubleClickEvent(QMouseEvent *) { frameSelection(); }

void Scene3DView::wheelEvent(QWheelEvent *e)
{
    const double steps = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->pixelDelta().y() * 2.0;
    m_cam.distance = std::clamp(float(m_cam.distance * std::pow(0.999, steps)), 0.05f, 500.0f);
    pushCamera();
    update();
    e->accept();
}

void Scene3DView::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_W: setMode(Mode::Move); break;
    case Qt::Key_E: setMode(Mode::Rotate); break;
    case Qt::Key_R: setMode(Mode::Scale); break;
    case Qt::Key_F: frameSelection(); break;
    default: QOpenGLWidget::keyPressEvent(e); return;
    }
    e->accept();
}
