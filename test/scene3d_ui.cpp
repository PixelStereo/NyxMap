// 3D scenes in the interface: Layer › New 3D Scene makes one with a light; a model dropped onto a layer goes into a new
// scene; the list refuses a move that takes a 3D object out of its scene or a 2D layer into one; the inspector shows the
// scene's camera (Scene tab) and an object's values (3D tab), each edit one undo step.
#include "Engine.h"
#include "LayerInspector.h"
#include "LayerTable.h"
#include "MediaBin.h"
#include "PreviewWorkspace.h"
#include "Scene3DView.h"
#include <QJsonArray>
#include <QMouseEvent>
#include "Model3D.h"
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include "MainWindow.h"
#include "Widgets.h"
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFormLayout>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QUndoStack>
#include <cstdio>

int runScene3dUiTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *reason) {
        if (!ok) { ++failures; std::printf("FAIL scene3d_ui: %s\n", reason); }
    };
    auto pump = [] {
        for (int k = 0; k < 3; ++k) {
            qApp->processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    };
    QTemporaryDir dir;
    const QString cube = dir.path() + "/cube.obj";
    {
        QFile f(cube);
        check(f.open(QIODevice::WriteOnly), "write the cube");
        f.write("v -0.5 -0.5 -0.5\nv 0.5 -0.5 -0.5\nv 0.5 0.5 -0.5\nv -0.5 0.5 -0.5\n"
                "v -0.5 -0.5 0.5\nv 0.5 -0.5 0.5\nv 0.5 0.5 0.5\nv -0.5 0.5 0.5\n"
                "f 1 3 2\nf 1 4 3\nf 5 6 7\nf 5 7 8\nf 1 5 8\nf 1 8 4\nf 2 3 7\nf 2 7 6\nf 1 2 6\nf 1 6 5\nf 4 8 7\nf 4 7 3\n");
    }
    engine.newProject();
    MainWindow window(&engine);
    window.setAutosaveEnabled(false);
    window.setQuiet(true);
    window.show();
    pump();
    auto *undo = window.findChild<QUndoStack *>();
    auto *list = window.findChild<LayerTable *>();
    auto action = [&](const QString &text) {
        for (QAction *a : window.findChildren<QAction *>())
            if (a->text() == text) return a->trigger(), true;
        return false;
    };
    auto current = [&] { return list ? list->currentRow() : -1; };
    auto select = [&](int row) {
        if (!list) return;
        list->setCurrentRow(row);
        emit list->currentRowChanged(row);
        pump();
    };
    auto roleOf = [&](quint64 id) {
        const LayerTree t = engine.structure();
        const int i = tree::indexOf(t, id);
        return i < 0 ? -1 : int(t[size_t(i)].role);
    };
    auto parentOf = [&](quint64 id) {
        const LayerTree t = engine.structure();
        const int i = tree::indexOf(t, id);
        return i < 0 ? 0 : t[size_t(i)].parent;
    };

    // A model dropped onto a plain layer: a scene is made around it, with a light, in one undo step
    check(list != nullptr, "the layer list");
    if (!list) return failures;
    emit list->addClicked();
    pump();
    const quint64 layer = engine.layerId(current());
    const int steps = undo ? undo->count() : 0;
    emit list->filesDropped(engine.indexOfId(layer), {cube});
    pump();
    const quint64 scene = parentOf(layer);
    check(scene && roleOf(scene) == TreeNode::Scene3d && roleOf(layer) == TreeNode::Object3d, "the model is put in a new 3D scene");
    int lights = 0;
    for (int m : engine.groupMembers(engine.indexOfId(scene))) {
        Engine::Lock lk(&engine.mutex());
        lights += engine.layer(m)->type == SourceType::Light;
    }
    check(lights == 1, "the new scene has a light");
    check(tree::valid3d(engine.structure()), "the structure keeps the 3D rule");
    check(undo && undo->count() == steps + 1, "loading into a scene is one undo step");
    if (undo) {
        undo->undo();
        pump();
        check(!parentOf(layer) && engine.indexOfId(scene) < 0, "undo takes the scene away");
        undo->redo();
        pump();
        check(parentOf(layer) == scene, "redo puts it back");
    }

    // Moves that break the rule are refused: the model out of its scene, a 2D layer into it
    const LayerTree before = engine.structure();
    emit list->moveRequested({engine.indexOfId(layer)}, -1, -1);
    pump();
    check(engine.structure() == before && parentOf(layer) == scene, "a 3D object stays in its scene");
    select(engine.indexOfId(layer)); // a new layer asked from inside the scene
    emit list->addClicked();
    pump();
    const quint64 plain = engine.layerId(current());
    check(!parentOf(plain), "a new layer is never made inside a scene");
    emit list->moveRequested({engine.indexOfId(plain)}, -1, engine.indexOfId(scene));
    pump();
    check(parentOf(plain) != scene, "a 2D layer is refused by a scene");

    // New 3D Scene, then a light added to it
    check(action("New 3D Scene"), "Layer › New 3D Scene");
    pump();
    const quint64 second = engine.layerId(current());
    check(roleOf(second) == TreeNode::Scene3d && engine.groupMembers(engine.indexOfId(second)).size() == 1, "New 3D Scene comes with a light");
    check(action("Add Light to 3D Scene"), "Layer › Add Light to 3D Scene");
    pump();
    check(engine.groupMembers(engine.indexOfId(second)).size() == 2, "Add Light puts a light into the selected scene");

    // The inspector: the scene's camera, an object's values; an edit is undoable
    LayerInspector *inspector = nullptr;
    for (LayerInspector *candidate : window.findChildren<LayerInspector *>())
        if (candidate->objectName() != "viewportInspector") inspector = candidate;
    auto tabNames = [&] {
        QStringList names;
        if (inspector)
            for (auto *tabs : inspector->findChildren<QTabWidget *>("layerCategoryTabs"))
                for (int k = 0; k < tabs->count(); ++k)
                    if (tabs->isTabVisible(k)) names << tabs->tabText(k).section(' ', 0, 0);
        return names;
    };
    select(engine.indexOfId(scene));
    check(tabNames().contains("Scene"), "a scene's inspector has its Scene tab");
    auto *yaw = inspector ? inspector->findChild<SliderField *>("param3d:camera/yaw") : nullptr;
    check(yaw != nullptr, "the camera's yaw is a bar");
    if (yaw && undo) {
        const int n = undo->count();
        emit yaw->valueEdited(90);
        check(std::abs(engine.parameterValue(scene, "camera/yaw").toDouble() - 90) < 1e-6 && undo->count() == n + 1,
              "turning the camera is one undo step");
        undo->undo();
        check(std::abs(engine.parameterValue(scene, "camera/yaw").toDouble() - SceneSettings().yaw) < 1e-6, "undo turns it back");
        // A click on its name puts its default back, one undo step
        emit yaw->valueEdited(120);
        auto *form = qobject_cast<QFormLayout *>(yaw->parentWidget()->layout());
        auto *name = form ? qobject_cast<ResetLabel *>(form->labelForField(yaw)) : nullptr;
        check(name != nullptr, "the yaw's name is a reset label");
        if (name) {
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(2, 2), name->mapToGlobal(QPointF(2, 2)), Qt::LeftButton, Qt::LeftButton, {});
            QApplication::sendEvent(name, &press);
            pump();
            check(std::abs(engine.parameterValue(scene, "camera/yaw").toDouble() - SceneSettings().yaw) < 1e-6,
                  "clicking the yaw's name resets it");
            undo->undo(); // the edit and its reset, merged: one step back to before them
            check(std::abs(engine.parameterValue(scene, "camera/yaw").toDouble() - SceneSettings().yaw) < 1e-6, "undo of the reset");
        }
    }
    select(engine.indexOfId(layer));
    const QStringList objectTabs = tabNames();
    check(objectTabs.contains("3D") && !objectTabs.contains("Spatial") && !objectTabs.contains("FX"),
          "a model's inspector shows its 3D values, not a picture's tabs");
    check(inspector && inspector->findChild<SliderField *>("param3d:model/scale/all") &&
              inspector->findChild<SliderField *>("param3d:model/scale/x") && inspector->findChild<SliderField *>("param3d:model/scale/z"),
          "the model's scale, overall and along each axis, are bars");
    check(inspector && inspector->findChild<QComboBox *>("modelPictureLayer") && inspector->findChild<QComboBox *>("modelDisplaceLayer"),
          "a model's 3D tab chooses the layer shown on it and the one that displaces it");
    if (auto *pick = inspector ? inspector->findChild<QComboBox *>("modelPictureLayer") : nullptr) {
        bool offersScene = false;
        for (int k = 0; k < pick->count(); ++k) offersScene |= pick->itemData(k).toULongLong() == scene;
        check(!offersScene, "its own scene is not offered (a loop)");
    }
    // A primitive from the Media Bin dropped onto a plain layer: into a new scene
    select(engine.indexOfId(plain));
    emit list->filesDropped(engine.indexOfId(plain), {Model3D::primitivePath("Sphere")});
    pump();
    check(roleOf(plain) == TreeNode::Object3d && roleOf(parentOf(plain)) == TreeNode::Scene3d, "a primitive goes into a new scene");
    check(engine.binItems().filter("/NyxMap/3D/").isEmpty(), "a primitive is never imported as a file");

    // The Media Bin files a model under 3D Models, read (not "unreadable"); the list has a 3D button
    check(window.findChild<QToolButton *>("layer3dButton") != nullptr, "the layer list has its 3D button");
    if (auto *bin = window.findChild<MediaBin *>()) {
        bin->importFiles({cube});
        QTreeWidgetItem *models = nullptr, *item = nullptr;
        for (int round = 0; round < 100 && !(item && item->text(1).contains("triangles")); ++round) {
            pump();
            QThread::msleep(10);
            for (QTreeWidget *tree : bin->findChildren<QTreeWidget *>())
                for (QTreeWidgetItem *found : tree->findItems("3D Models", Qt::MatchStartsWith | Qt::MatchRecursive)) models = found;
            item = models && models->childCount() ? models->child(0) : nullptr;
        }
        check(models && models->text(0) == "3D Models (1)", "the Media Bin has a 3D Models category holding the cube");
        check(item && item->text(1).contains("12 triangles"), "the Media Bin reads the model");
        int primitives = 0;
        for (QTreeWidget *tree : bin->findChildren<QTreeWidget *>())
            for (QTreeWidgetItem *found : tree->findItems("3D Primitives", Qt::MatchStartsWith | Qt::MatchRecursive))
                primitives = found->childCount();
        check(primitives == int(Model3D::primitives().size()), "the Media Bin offers the 3D primitives");
    } else {
        check(false, "the Media Bin");
    }
    // A 3D view: the scene chosen in a preview cell is seen from the view's own camera; the cube is picked and moved
    auto *previews = window.findChild<PreviewWorkspace *>();
    check(previews != nullptr, "the preview workspace");
    if (previews) {
        previews->setState(QJsonObject{{"count", 1}, {"cells", QJsonArray{QJsonObject{{"source", QString::number(scene)}}}}});
        pump();
        auto *view = window.findChild<Scene3DView *>("scene3dView0");
        check(view && view->isVisible() && view->scene() == scene, "a 3D scene in a view shows its 3D view");
        if (view) {
            view->resize(480, 320);
            SceneSettings cam;
            cam.distance = 4;
            cam.yaw = 30;
            cam.pitch = 20;
            view->setCamera(cam);
            for (int k = 0; k < 6; ++k) {
                engine.renderFrame();
                pump();
            }
            const QImage shot = view->grabFramebuffer();
            const QColor middle = shot.pixelColor(shot.width() / 2, shot.height() / 2);
            check(middle.lightness() > 60, "the 3D view draws the scene's model");
            // Click the cube: selected; drag its X handle: it moves along X, one undo step
            const QPointF at = view->project({0, 0, 0});
            auto mouse = [&](QEvent::Type type, QPointF p, Qt::MouseButtons buttons) {
                QMouseEvent ev(type, p, view->mapToGlobal(p), Qt::LeftButton, buttons, {});
                QApplication::sendEvent(view, &ev);
                pump();
            };
            mouse(QEvent::MouseButtonPress, at, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, at, Qt::NoButton);
            check(current() == engine.indexOfId(layer), "a click picks the cube (selected in the list)");
            view->setMode(Scene3DView::Mode::Move);
            const QPointF handle = view->project({view->handleLength() * 0.8f, 0, 0});
            const QPointF along = view->project({view->handleLength() * 1.8f, 0, 0});
            const int n = undo ? undo->count() : 0;
            const double x0 = engine.parameterValue(layer, "model/position/x").toDouble();
            mouse(QEvent::MouseButtonPress, handle, Qt::LeftButton);
            mouse(QEvent::MouseMove, along, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, along, Qt::NoButton);
            const double x1 = engine.parameterValue(layer, "model/position/x").toDouble();
            check(x1 > x0 + view->handleLength() * 0.5, "dragging the X handle moves the cube along X");
            check(engine.parameterValue(layer, "model/position/y").toDouble() == 0, "only along X");
            check(undo && undo->count() == n + 1 && undo->text(undo->count() - 1) == "Move in 3D", "the move is one undo step");
            if (undo) {
                undo->undo();
                check(std::abs(engine.parameterValue(layer, "model/position/x").toDouble() - x0) < 1e-9, "undo puts it back");
            }
            // The scene's camera takes the view's
            view->sceneCameraFromView();
            check(std::abs(engine.parameterValue(scene, "camera/yaw").toDouble() - 30) < 1e-6 &&
                      std::abs(engine.parameterValue(scene, "camera/pitch").toDouble() - 20) < 1e-6,
                  "Camera = View gives the scene the view's camera");
            if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) {
                view->setSelected(layer);
                view->setMode(Scene3DView::Mode::Move);
                engine.setParameter(scene, "camera/yaw", -55.0); // the scene's camera elsewhere: seen in the view
                SceneSettings wide = view->camera();
                wide.distance = 9;
                view->setCamera(wide);
                for (int k = 0; k < 3; ++k) {
                    engine.renderFrame();
                    pump();
                }
                view->grabFramebuffer().save(captures + "/scene3d-view.png");
            }
        }
    }
    if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) {
        for (int k = 0; k < 5; ++k) {
            engine.renderFrame();
            pump();
        }
        window.grab().save(captures + "/scene3d-window.png");
    }
    window.hide();
    return failures;
}
