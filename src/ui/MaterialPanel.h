#pragma once
#include "Engine.h"
#include "Model3D.h"
#include "ColorPicker.h"
#include <QOpenGLWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QTimer>
#include <optional>

// A separate, on-demand studio scene: never adds a layer or changes the show's camera.
class MaterialSphere : public QOpenGLWidget {
public:
    explicit MaterialSphere(QWidget *parent = nullptr) : QOpenGLWidget(parent) {
        setObjectName("materialSphere");
        setMinimumSize(160, 160);
        setMaximumHeight(320);
        sphere = std::make_shared<Model3D>();
        sphere->load(Model3D::primitivePath("Sphere"), nullptr);
    }
    ~MaterialSphere() override { if (context()) disconnect(context(), nullptr, this, nullptr); cleanup(); }
    void setMaterial(const Material3D &m) { material = m; update(); }
protected:
    void initializeGL() override {
        connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] { cleanup(); }, Qt::DirectConnection);
        gl()->glGenFramebuffers(1, &readFbo);
    }
    void paintGL() override {
        const int w = std::max(1, int(width() * devicePixelRatioF()));
        const int h = std::max(1, int(height() * devicePixelRatioF()));
        // Fixed maximum render size, no timer: repaint only when material or widget changes.
        const double scale = std::min(1.0, 512.0 / std::max(w, h));
        const int sw = std::max(1, int(w * scale)), sh = std::max(1, int(h * scale));
        SceneSettings settings;
        settings.distance = 2.2f;
        settings.background = {{.035f, .035f, .045f, 1}};
        settings.ao = settings.reflections = settings.glow = 0;
        Scene3D::Item item;
        item.model = sphere;
        item.placement.color = material.color;
        item.placement.metal = material.metal;
        item.placement.rough = material.rough;
        item.placement.sharedMaterial = true;
        sphere->setDetail(3);
        LightSettings light;
        light.shadow = false;
        Scene3DQuality quality;
        quality.antialiasing = quality.shadows = quality.contact = quality.reflections = quality.glow = quality.atmosphere = 0;
        const GLuint texture = scene.draw(settings, {item}, {{light, 1}}, sw, sh, 0, false, quality);
        gl()->glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
        gl()->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        gl()->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
        gl()->glBlitFramebuffer(0, 0, sw, sh, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        gl()->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    }
private:
    void cleanup() {
        if (!context() || !readFbo) return;
        makeCurrent();
        scene.releaseGl();
        sphere->releaseGl();
        gl()->glDeleteFramebuffers(1, &readFbo);
        readFbo = 0;
        doneCurrent();
    }
    Scene3D scene;
    std::shared_ptr<Model3D> sphere;
    Material3D material;
    GLuint readFbo = 0;
};

class MaterialPanel : public QWidget {
public:
    MaterialPanel(Engine *engine, QUndoStack *undo, std::function<int()> currentObject, std::function<void()> edited,
                  QWidget *parent = nullptr) : QWidget(parent), engine(engine), undo(undo), edited(edited) {
        setObjectName("materialPanel");
        auto *layout = new QVBoxLayout(this);
        auto *tools = new QHBoxLayout;
        auto button = [&](const QString &label, const QString &name, auto action) {
            auto *b = new QPushButton(label); b->setObjectName(name); tools->addWidget(b);
            connect(b, &QPushButton::clicked, this, action); return b;
        };
        button("+", "addMaterial", [this] {
            const quint64 id = this->engine->addMaterial();
            Material3D m;
            { Engine::Lock lock(&this->engine->mutex()); m = *this->engine->materials().at(id); }
            this->engine->removeUnusedMaterial(id);
            commit({}, m); selected = id; refresh();
        });
        button("Duplicate", "duplicateMaterial", [this] {
            const auto m = current(); if (!m) return;
            Material3D copy = *m;
            copy.id = this->engine->addMaterial();
            this->engine->removeUnusedMaterial(copy.id);
            copy.name += " copy";
            commit({}, copy); selected = copy.id; refresh();
        });
        remove = button("−", "removeMaterial", [this] { const auto m = current(); if (m && !users(m->id)) commit(m, {}); });
        layout->addLayout(tools);
        list = new QListWidget; list->setObjectName("materialList"); list->setMaximumHeight(150); layout->addWidget(list);
        sphere = new MaterialSphere; layout->addWidget(sphere);
        auto *note = new QLabel("Studio preview · base color, metallic and roughness"); note->setWordWrap(true); layout->addWidget(note);
        auto *form = new QFormLayout;
        name = new QLineEdit; name->setObjectName("materialName"); form->addRow("Name", name);
        layout->addLayout(form);
        color = new ColorPicker(false); color->setSettingsKey("materials/browser/color"); layout->addWidget(color);
        auto *surface = new QFormLayout;
        metal = spin("materialMetal"); rough = spin("materialRough");
        surface->addRow("Metallic", metal); surface->addRow("Roughness", rough); layout->addLayout(surface);
        usage = new QLabel; usage->setWordWrap(true); layout->addWidget(usage);
        auto *assign = new QPushButton("Assign to selected 3D object"); assign->setObjectName("assignMaterial"); layout->addWidget(assign);
        connect(assign, &QPushButton::clicked, this, [this, currentObject] {
            const int row = currentObject();
            quint64 object = 0, before = 0;
            { Engine::Lock lock(&this->engine->mutex()); const auto *l = this->engine->layer(row);
              if (!l || l->type != SourceType::Model || this->engine->isLocked(row) || !current()) return;
              object = l->id; before = l->modelMaterial; }
            class Assign : public QUndoCommand {
            public:
                Assign(Engine *e, quint64 o, quint64 b, quint64 a) : QUndoCommand("Assign Material"), e(e), o(o), b(b), a(a) {}
                void redo() override { e->setModelMaterial(e->indexOfId(o), a); }
                void undo() override { e->setModelMaterial(e->indexOfId(o), b); }
                Engine *e; quint64 o, b, a;
            };
            this->undo->push(new Assign(this->engine, object, before, selected));
            this->edited(); refresh();
        });
        connect(list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
            selected = item ? item->data(Qt::UserRole).toULongLong() : 0; showCurrent();
        });
        connect(name, &QLineEdit::editingFinished, this, [this] { change([this](Material3D &m) { m.name = name->text(); }); });
        connect(color, &ColorPicker::colorEdited, this, [this](const QColor &c) { change([&](Material3D &m) { m.color = {{float(c.redF()), float(c.greenF()), float(c.blueF())}}; }); });
        connect(metal, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) { change([&](Material3D &m) { m.metal = float(v); }); });
        connect(rough, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) { change([&](Material3D &m) { m.rough = float(v); }); });
        connect(engine, &Engine::layersChanged, this, [this] { refresh(); });
        auto *timer = new QTimer(this); timer->setInterval(500);
        connect(timer, &QTimer::timeout, this, [this] { if (isVisible()) refresh(); }); timer->start();
        refresh();
    }
private:
    QDoubleSpinBox *spin(const QString &object) {
        auto *s = new QDoubleSpinBox; s->setObjectName(object); s->setRange(0, 1); s->setDecimals(3); s->setSingleStep(.01); return s;
    }
    std::optional<Material3D> current() const {
        Engine::Lock lock(&engine->mutex()); const auto it = engine->materials().find(selected);
        return it == engine->materials().end() ? std::optional<Material3D>{} : *it->second;
    }
    int users(quint64 id) const {
        Engine::Lock lock(&engine->mutex()); int count = 0;
        for (int i = 0; i < engine->layerCount(); ++i) { const auto *l = engine->layer(i); if (l && l->type == SourceType::Model && l->modelMaterial == id) ++count; }
        return count;
    }
    template<class F> void change(F edit) { auto before = current(); if (!before) return; auto after = *before; edit(after); if (before->toJson() != after.toJson()) commit(before, after); }
    void commit(std::optional<Material3D> before, std::optional<Material3D> after) {
        class Edit : public QUndoCommand {
        public:
            Edit(Engine *e, std::optional<Material3D> b, std::optional<Material3D> a) : QUndoCommand("Edit Material"), e(e), b(b), a(a) {}
            void apply(const std::optional<Material3D> &m, const std::optional<Material3D> &other) {
                { Engine::Lock lock(&e->mutex());
                  if (m) { e->restoreMaterial(*m); *e->materials().at(m->id) = *m; }
                  else if (other) e->removeUnusedMaterial(other->id); }
                emit e->layersChanged();
            }
            void redo() override { apply(a, b); }
            void undo() override { apply(b, a); }
            Engine *e; std::optional<Material3D> b, a;
        };
        undo->push(new Edit(engine, before, after)); edited(); refresh();
    }
    void refresh() {
        QJsonArray state, names;
        { Engine::Lock lock(&engine->mutex());
          std::map<quint64, int> counts;
          for (int i = 0; i < engine->layerCount(); ++i) {
              const auto *l = engine->layer(i);
              if (l && l->type == SourceType::Model) ++counts[l->modelMaterial];
          }
          for (const auto &[id, m] : engine->materials()) {
              auto json = m->toJson(); json["users"] = counts[id]; state.append(json);
              names.append(QJsonObject{{"id", QString::number(id)}, {"name", m->name}});
          } }
        if (state == known && listedSelection == selected) return;
        known = state;
        if (names == knownNames && listedSelection == selected) { showCurrent(); return; }
        knownNames = names;
        QSignalBlocker block(list);
        list->clear();
        { Engine::Lock lock(&engine->mutex());
          for (const auto &[id, m] : engine->materials()) { auto *item = new QListWidgetItem(m->name, list); item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(id)); if (id == selected) list->setCurrentItem(item); } }
        if (!list->currentItem() && list->count()) list->setCurrentRow(0);
        selected = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toULongLong() : 0;
        listedSelection = selected;
        showCurrent();
    }
    void showCurrent() {
        const auto m = current();
        for (auto *w : {static_cast<QWidget *>(name), static_cast<QWidget *>(color), static_cast<QWidget *>(metal), static_cast<QWidget *>(rough)}) w->setEnabled(bool(m));
        if (!m) { usage->setText("No material selected"); remove->setEnabled(false); return; }
        const QSignalBlocker n(name), c(color), mt(metal), r(rough);
        name->setText(m->name); color->setColor(QColor::fromRgbF(m->color[0], m->color[1], m->color[2])); metal->setValue(m->metal); rough->setValue(m->rough);
        const int count = users(m->id); remove->setEnabled(count == 0);
        usage->setText(QStringLiteral("Used by %1 object(s) · shared across scenes").arg(count));
        if (shown != m->toJson()) { shown = m->toJson(); sphere->setMaterial(*m); }
    }
    Engine *engine; QUndoStack *undo; std::function<void()> edited;
    quint64 selected = 0, listedSelection = ~quint64(0); QJsonObject shown; QJsonArray known, knownNames;
    QListWidget *list; MaterialSphere *sphere; QLineEdit *name; ColorPicker *color;
    QDoubleSpinBox *metal, *rough; QPushButton *remove; QLabel *usage;
};
