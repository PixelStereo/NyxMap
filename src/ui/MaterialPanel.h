#pragma once
#include "Engine.h"
#include "Model3D.h"
#include "MaterialX.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QToolButton>
#include "ColorPicker.h"
#include <QOpenGLWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QListWidget>
#include <QLineEdit>
#include "Widgets.h"
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
    void setMaterial(const Material3D &m) {
        if (material.toJson() == m.toJson()) return;
        material = m; update();
    }
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
        textures.advance();
        textures.apply(material, item.placement);
        if (item.placement.materialTextures[HeightMap]) {
            item.displace = item.placement.materialTextures[HeightMap];
            item.displaceAmount = material.height;
            item.placement.materialHeight = true;
        }
        sphere->setDetail(item.displace ? 6 : 3);
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
        textures.release();
        sphere->releaseGl();
        gl()->glDeleteFramebuffers(1, &readFbo);
        readFbo = 0;
        doneCurrent();
    }
    Scene3D scene;
    MaterialTextureCache textures;
    std::shared_ptr<Model3D> sphere;
    Material3D material;
    GLuint readFbo = 0;
};

class MaterialPanel : public QWidget {
public:
    MaterialPanel(Engine *engine, QUndoStack *undo, std::function<int()> currentObject, std::function<void()> edited,
                  QWidget *parent = nullptr, QWidget *objectSurface = nullptr)
        : QWidget(parent), engine(engine), undo(undo), edited(edited),
          inspectedObject(objectSurface ? currentObject : std::function<int()>{}) {
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
        button("Import .mtlx…", "importMaterialX", [this] {
            const QString path = QFileDialog::getOpenFileName(this, "Import MaterialX", {}, "MaterialX (*.mtlx)");
            if (path.isEmpty()) return;
            Material3D material;
            QString error;
            if (!importMaterialX(path, material, &error)) {
                QMessageBox::warning(this, "Import MaterialX", error);
                return;
            }
            addImportedMaterial(std::move(material));
        });
        button("Import glTF…", "importMaterialGltf", [this] {
            const QString path = QFileDialog::getOpenFileName(this, "Import glTF Material", {}, "glTF (*.gltf *.glb)");
            if (path.isEmpty()) return;
            QString error;
            auto materials = importGltfMaterials(path, &error);
            if (materials.empty()) {
                QMessageBox::warning(this, "Import glTF Material", error);
                return;
            }
            int index = 0;
            if (materials.size() > 1) {
                QStringList choices;
                for (size_t i = 0; i < materials.size(); ++i)
                    choices << QStringLiteral("%1 · %2").arg(i + 1).arg(materials[i].name);
                bool accepted = false;
                const auto choice = QInputDialog::getItem(this, "Import glTF Material", "Material", choices, 0, false, &accepted);
                if (!accepted) return;
                index = choices.indexOf(choice);
                if (index < 0) return;
            }
            addImportedMaterial(std::move(materials[size_t(index)]));
        });
        remove = button("−", "removeMaterial", [this] { const auto m = current(); if (m && !users(m->id)) commit(m, {}); });
        layout->addLayout(tools);
        if (inspectedObject)
            for (int i = 0; i < tools->count(); ++i) tools->itemAt(i)->widget()->hide();
        list = new QListWidget; list->setObjectName("materialList"); list->setMaximumHeight(150); layout->addWidget(list);
        list->setVisible(!inspectedObject);
        sphere = new MaterialSphere; layout->addWidget(sphere);
        auto *note = new QLabel("Studio preview · physically based material"); note->setWordWrap(true); layout->addWidget(note);
        auto *form = new QFormLayout;
        name = new QLineEdit; name->setObjectName("materialName"); form->addRow("Name", name);
        layout->addLayout(form);
        auto *basic = new QWidget;
        auto *basicLayout = new QVBoxLayout(basic);
        basicLayout->setContentsMargins(0, 0, 0, 0);
        color = new ColorPicker(false);
        color->setObjectName("materialBaseColor");
        color->setSettingsKey("materials/browser/color");
        basicLayout->addWidget(color);
        auto *surface = new QFormLayout;
        metal = spin("materialMetal"); rough = spin("materialRough");
        surface->addRow("Metallic", metal); surface->addRow("Roughness", rough); basicLayout->addLayout(surface);
        layout->addWidget(basic);
        if (objectSurface) {
            basic->hide();
            layout->addWidget(objectSurface);
        }
        auto *maps = new QFormLayout;
        const char *labels[] = {"Base Color", "Roughness", "Metallic", "Normal", "Ambient Occlusion", "Emission", "Displacement"};
        for (int i = 0; i < MaterialMapCount; ++i) {
            auto *row = new QHBoxLayout;
            mapButtons[i] = new QPushButton("Choose Image…");
            mapButtons[i]->setObjectName(QStringLiteral("materialMap_%1").arg(QString::fromLatin1(materialMapNames[i])));
            mapButtons[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            clearButtons[i] = new QToolButton;
            clearButtons[i]->setText(QStringLiteral("×"));
            clearButtons[i]->setToolTip("Remove image");
            row->addWidget(mapButtons[i], 1);
            row->addWidget(clearButtons[i]);
            maps->addRow(QString::fromLatin1(labels[i]), row);
            connect(mapButtons[i], &QPushButton::clicked, this, [this, slot = MaterialMap(i)] {
                const auto before = current();
                if (!before) return;
                const QString path = QFileDialog::getOpenFileName(this, "Choose Material Image", before->paths[slot]);
                if (path.isEmpty()) return;
                auto after = *before;
                QString error;
                if (!after.setMap(slot, path, &error)) {
                    QMessageBox::warning(this, "Choose Material Image", error);
                    return;
                }
                if (before->images[slot].isNull()) {
                    if (slot == MetallicMap) after.metal = 1;
                    if (slot == RoughnessMap) after.rough = 1;
                    if (slot == BaseColorMap) after.color = {{1, 1, 1}};
                    if (slot == EmissionMap && after.emission == 0) after.emission = 1;
                }
                commit(before, after);
            });
            connect(clearButtons[i], &QToolButton::clicked, this, [this, slot = MaterialMap(i)] {
                change([slot](Material3D &material) { material.setMap(slot, {}); });
            });
        }
        layout->addLayout(maps);
        auto *details = new QFormLayout;
        for (const auto &[label, member, lo, hi] : std::initializer_list<Scalar>{
                {"Normal Strength", &Material3D::normalStrength, 0, 10},
                {"Occlusion Strength", &Material3D::occlusionStrength, 0, 1},
                {"Emission Strength", &Material3D::emission, 0, 100},
                {"Displacement Scale", &Material3D::height, -2, 2},
                {"Specular IOR", &Material3D::ior, 1, 4},
                {"Specular Weight", &Material3D::specular, 0, 1}}) {
            auto *field = spin(QString::fromLatin1(label));
            field->setRange(lo, hi);
            details->addRow(QString::fromLatin1(label), field);
            scalarFields.emplace_back(field, member);
            connect(field, &SliderField::valueEdited, this, [this, member](double v) {
                change([&](Material3D &material) { material.*member = float(v); });
            });
        }
        emissionColor = new ColorPicker(false);
        emissionColor->setSettingsKey("materials/browser/emissionColor");
        details->addRow("Emission Color", emissionColor);
        connect(emissionColor, &ColorPicker::colorEdited, this, [this](const QColor &c) {
            change([&](Material3D &material) { material.emissionColor = {{float(c.redF()), float(c.greenF()), float(c.blueF())}}; });
        });
        for (int axis = 0; axis < 2; ++axis) {
            tiling[axis] = spin(axis == 0 ? "tileU" : "tileV");
            tiling[axis]->setRange(.01, 100);
            details->addRow(axis == 0 ? "UV Repeat U" : "UV Repeat V", tiling[axis]);
            connect(tiling[axis], &SliderField::valueEdited, this, [this, axis](double v) {
                change([&](Material3D &material) { material.uvTiling[axis] = float(v); });
            });
        }
        layout->addLayout(details);
        usage = new QLabel; usage->setWordWrap(true); layout->addWidget(usage);
        auto *assign = new QPushButton("Assign to selected 3D object"); assign->setObjectName("assignMaterial"); layout->addWidget(assign);
        assign->setVisible(!inspectedObject);
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
        connect(metal, &SliderField::valueEdited, this, [this](double v) { change([&](Material3D &m) { m.metal = float(v); }); });
        connect(rough, &SliderField::valueEdited, this, [this](double v) { change([&](Material3D &m) { m.rough = float(v); }); });
        connect(engine, &Engine::layersChanged, this, [this] { refresh(); });
        auto *timer = new QTimer(this); timer->setInterval(500);
        connect(timer, &QTimer::timeout, this, [this] { if (isVisible()) refresh(); }); timer->start();
        refresh();
    }
private:
    struct Scalar { const char *label; float Material3D::*member; double min, max; };
    std::vector<std::pair<SliderField *, float Material3D::*>> scalarFields;
    std::array<QPushButton *, MaterialMapCount> mapButtons{};
    std::array<QToolButton *, MaterialMapCount> clearButtons{};
    std::array<SliderField *, 2> tiling{};
    ColorPicker *emissionColor = nullptr;

    SliderField *spin(const QString &object) {
        auto *s = new SliderField; s->setObjectName(object); s->setRange(0, 1); s->setDecimals(3); s->setSingleStep(.01); return s;
    }
    std::optional<Material3D> current() const {
        Engine::Lock lock(&engine->mutex()); const auto it = engine->materials().find(selected);
        return it == engine->materials().end() ? std::optional<Material3D>{} : *it->second;
    }
    int users(quint64 id) const {
        Engine::Lock lock(&engine->mutex()); int count = 0;
        for (int i = 0; i < engine->layerCount(); ++i) { const auto *l = engine->layer(i); if (l && ((l->type == SourceType::Model && l->modelMaterial == id) || (l->scene3d && l->sceneDefaultMaterial == id))) ++count; }
        return count;
    }
    template<class F> void change(F edit) { auto before = current(); if (!before) return; auto after = *before; edit(after); if (before->toJson() != after.toJson()) commit(before, after); }
    void addImportedMaterial(Material3D material) {
        material.id = engine->addMaterial(material.name);
        engine->removeUnusedMaterial(material.id);
        commit({}, material);
        selected = material.id;
        refresh();
    }
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
        if (inspectedObject && engine->isLocked(inspectedObject())) return;
        auto *command = new Edit(engine, before, after);
        if (undo) undo->push(command);
        else { command->redo(); delete command; }
        edited(); refresh();
    }
    void refresh() {
        if (inspectedObject) {
            QJsonObject state;
            {
                Engine::Lock lock(&engine->mutex());
                const auto *object = engine->layer(inspectedObject());
                selected = object ? object->modelMaterial : 0;
                if (object && object->material) {
                    state = object->material->toJson();
                    state["users"] = users(selected);
                }
            }
            if (state != inspectedState || listedSelection != selected) {
                inspectedState = state;
                listedSelection = selected;
                showCurrent();
            }
            return;
        }
        QJsonArray state, names;
        { Engine::Lock lock(&engine->mutex());
          std::map<quint64, int> counts;
          for (int i = 0; i < engine->layerCount(); ++i) {
              const auto *l = engine->layer(i);
              if (l && l->type == SourceType::Model) ++counts[l->modelMaterial];
              if (l && l->scene3d) ++counts[l->sceneDefaultMaterial];
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
        const bool editable = m && (!inspectedObject || !engine->isLocked(inspectedObject()));
        for (auto *w : {static_cast<QWidget *>(name), static_cast<QWidget *>(color), static_cast<QWidget *>(metal), static_cast<QWidget *>(rough)}) w->setEnabled(editable);
        for (int i = 0; i < MaterialMapCount; ++i) {
            const QString path = m ? m->paths[i] : QString();
            mapButtons[i]->setEnabled(editable);
            const bool embedded = m && !m->embeddedMaps[i].isEmpty();
            const QString imageName = embedded ? m->embeddedMapNames[i] : QFileInfo(path).fileName();
            mapButtons[i]->setText(!imageName.isEmpty() ? imageName : embedded ? "Embedded Image" : "Choose Image…");
            mapButtons[i]->setToolTip(path.isEmpty() ? imageName : path);
            clearButtons[i]->setEnabled(editable && (embedded || !path.isEmpty()));
        }
        for (const auto &[field, member] : scalarFields) {
            field->setEnabled(editable);
            if (m) field->setValue((*m).*member);
        }
        for (int i = 0; i < 2; ++i) {
            tiling[i]->setEnabled(editable);
            if (m) tiling[i]->setValue(m->uvTiling[i]);
        }
        emissionColor->setEnabled(editable);
        if (m) emissionColor->setColor(QColor::fromRgbF(m->emissionColor[0], m->emissionColor[1], m->emissionColor[2]));
        if (!m) { usage->setText("No project material selected"); remove->setEnabled(false); sphere->setMaterial(Material3D()); return; }
        const QSignalBlocker n(name), c(color), mt(metal), r(rough);
        if (!name->hasFocus()) name->setText(m->name);
        color->setColor(QColor::fromRgbF(m->color[0], m->color[1], m->color[2])); metal->setValue(m->metal); rough->setValue(m->rough);
        const int count = users(m->id); remove->setEnabled(count == 0);
        usage->setText(QStringLiteral("References: %1 · objects and scene defaults").arg(count));
        sphere->setMaterial(*m);
    }
    Engine *engine; QUndoStack *undo; std::function<void()> edited;
    std::function<int()> inspectedObject;
    QJsonObject inspectedState;
    quint64 selected = 0, listedSelection = ~quint64(0); QJsonArray known, knownNames;
    QListWidget *list; MaterialSphere *sphere; QLineEdit *name; ColorPicker *color;
    SliderField *metal, *rough; QPushButton *remove; QLabel *usage;
};
