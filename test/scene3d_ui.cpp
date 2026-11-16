#include <QElapsedTimer>
#include "InteractionPanel.h"
#include "AdsrCurve.h"
// 3D scenes in the interface: Layer › New 3D Scene makes one with a light; a model dropped onto a layer goes into a new
// scene; the list refuses a move that takes a 3D object out of its scene or a 2D layer into one; the inspector shows the
// scene's camera (Scene tab) and an object's values (3D tab), each edit one undo step.
#include "Engine.h"
#include "fx3d_test.h"
#include "LayerInspector.h"
#include "LayerTable.h"
#include "FxChainPanel.h"
#include "MediaBin.h"
#include "MaterialPanel.h"
#include "PreviewWorkspace.h"
#include "Scene3DView.h"
#include <QCheckBox>
#include <QLabel>
#include <QMenu>
#include <QAction>
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
    engine.setCompositionSize({480, 270});
    engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {480, 270});
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
    // What another layer uses is not deleted without a warning; a source is found in the Media Bin
    {
        const int source = engine.addLayer("Used as a source");
        const quint64 sourceId = engine.layerId(source);
        const int user = engine.addLayer("Its user");
        const quint64 userId = engine.layerId(user);
        check(engine.setLayerSourceLayer(engine.indexOfId(userId), sourceId, LayerTap::PostFx), "a layer as another's source");
        const QSet<int> alone{engine.indexOfId(sourceId)}, both{engine.indexOfId(sourceId), engine.indexOfId(userId)};
        const QString used = window.layersInUse(alone);
        check(used.contains("Its user") && used.contains("source"), "deleting it says who uses it");
        check(window.layersInUse(both).isEmpty(), "not when they go together");
        check(window.layersInUse({engine.indexOfId(userId)}).isEmpty(), "nothing uses its user");
        // Its users' users come with the button; a mask is said, but is not deleted with it
        const int second = engine.addLayer("Its user's user");
        const quint64 secondId = engine.layerId(second);
        check(engine.setLayerSourceLayer(engine.indexOfId(secondId), userId, LayerTap::PostFx), "a layer made from that one");
        const int masked = engine.addLayer("Masked by it");
        const quint64 maskedId = engine.layerId(masked);
        check(engine.setColorMask(engine.indexOfId(maskedId), sourceId, false), "it masks another layer");
        const QSet<int> only{engine.indexOfId(sourceId)}, users = window.sourceUsers(only);
        check(users.size() == 2 && users.contains(engine.indexOfId(userId)) && users.contains(engine.indexOfId(secondId)), "the layers made from it, and from those");
        check(window.layersInUse(only).contains("color mask"), "a mask is said");
        Interaction reader;
        reader.source = InteractionSource::Internal;
        reader.layer = sourceId;
        reader.param = "opacity";
        reader.name = "Reads it";
        engine.addInteraction(reader);
        Animation moved;
        moved.name = "Moves it";
        AnimTrack track;
        track.layer = sourceId;
        track.param = "opacity";
        moved.tracks.push_back(track);
        const int timeline = engine.addAnimation(moved);
        const QString everywhere = window.layersInUse(only);
        check(everywhere.contains("Reads it") && everywhere.contains("Moves it"), "and an interaction and a timeline too");
        engine.removeAnimation(timeline);
        engine.setInteractions({});
        engine.removeLayer(engine.indexOfId(maskedId));
        engine.removeLayer(engine.indexOfId(secondId));
        engine.removeLayer(engine.indexOfId(userId));
        engine.removeLayer(engine.indexOfId(sourceId));
        pump();
        auto *bin = window.findChild<MediaBin *>();
        check(bin && !bin->reveal("/nowhere/such.mov"), "what is not in the bin is said so");
    }
    // Envelopes: in the hand on an instrument, in one go on any value, and the interactions as nodes
    {
        const QString recipe = dir.path() + "/tone.nyxaudio";
        {
            QFile f(recipe);
            check(f.open(QIODevice::WriteOnly), "write an instrument");
            f.write(R"({"version":1,"type":"fm","frequency":220,"gain":0.1,"attack":0.1,"decay":0.2,"sustain":0.5,"release":0.3})");
        }
        QString why;
        const int at = engine.addLayer("Tone");
        check(engine.setLayerAudio(at, recipe, &why), "an instrument on a layer");
        const quint64 tone = engine.layerId(at);
        const int second = engine.addLayer("Other");
        const quint64 other = engine.layerId(second);
        select(engine.indexOfId(tone));
        pump();
        auto *curve = static_cast<AdsrCurve *>(window.findChild<QWidget *>("adsrCurve"));
        check(curve != nullptr, "an instrument's envelope is a curve");
        if (curve && curve->isVisible()) {
            const QPointF from = curve->point(0), to = from + QPointF(30, 0);
            const auto mouse = [&](QEvent::Type type, const QPointF &p, Qt::MouseButtons buttons) {
                QMouseEvent ev(type, p, curve->mapToGlobal(p), Qt::LeftButton, buttons, {});
                QApplication::sendEvent(curve, &ev);
            };
            mouse(QEvent::MouseButtonPress, from, Qt::LeftButton);
            mouse(QEvent::MouseMove, to, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, to, Qt::NoButton);
            pump();
            check(engine.parameterValue(tone, "synth/attack").toDouble() > 0.1, "dragging its point lengthens its attack");
            check(std::abs(engine.parameterValue(tone, "synth/sustain").toDouble() - 0.5) < 1e-3, "and leaves its sustain");
            // Faust's envelopes, chosen by name: each with what it has
            auto *envelopes = window.findChild<QComboBox *>("synthEnvelope");
            check(envelopes && envelopes->count() == 7, "all of Faust's ADSRs to choose from");
            check(engine.setParameter(tone, "synth/envelope", QStringLiteral("ahdsrf_bias")), "by name");
            check(engine.parameterValue(tone, "synth/envelope").toString() == "ahdsrf_bias", "and read back");
            check(engine.setParameter(tone, "synth/envelope", QStringLiteral("adsr")), "and back to the first");
            auto *trigger = window.findChild<QPushButton *>("adsrTrigger");
            check(trigger != nullptr, "a button triggers it");
            if (trigger) {
                trigger->pressed();
                pump();
                check(engine.parameterValue(tone, "synth/gate").toDouble() == 1, "held: its gate is open");
                QElapsedTimer waited;
                waited.start();
                while (curve->playingLevel() <= 0 && waited.elapsed() < 500) pump();
                check(curve->playingLevel() > 0, "and the curve shows it running");
                trigger->released();
                pump();
                check(engine.parameterValue(tone, "synth/gate").toDouble() == 0, "let go: its gate closes");
            }
        }
        auto *panel = window.findChild<InteractionPanel *>();
        check(panel != nullptr, "the interactions' panel");
        if (panel) {
            engine.setParameter(other, "opacity", 0.25);
            const quint64 id = panel->addEnvelope(other, "opacity", InteractionPanel::heard(35, 120));
            pump();
            const auto find = [&](quint64 wanted) {
                for (const Interaction &it : engine.interactions())
                    if (it.id == wanted) return it;
                return Interaction();
            };
            const Interaction made = find(id);
            check(made.audioFeature == Interaction::AudioFeature::Hit && made.layer == other, "one click: a hit drives the value");
            check(made.filters.size() == 2 && made.filters[0].kind == InteractionFilter::Envelope, "through an envelope");
            check(made.filters.size() == 2 && std::abs(made.filters[1].c - 0.25) < 1e-6 && std::abs(made.filters[1].d - 1) < 1e-6, "from the value where it is to the end of its range");
            const quint64 gate = panel->addEnvelope(tone, "synth/gate", InteractionPanel::heard(35, 120));
            pump();
            check(find(gate).filters.empty(), "a gate takes the hit as it is: its instrument has its envelope");
            check(!panel->findChild<QToolButton *>("interactionNodes"), "node view is absent");
            check(panel->findChild<QWidget *>("interactionTargetTabs") &&
                      panel->findChild<QWidget *>("sharedFiltersFrame"), "target tabs follow the shared filter stack");
        }
        engine.setInteractions({});
        engine.removeLayer(engine.indexOfId(other));
        engine.removeLayer(engine.indexOfId(tone));
        pump();
    }
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
    {
        engine.renderFrame();
        pump();
        auto *count = inspector ? inspector->findChild<QLabel *>("sceneTriangles") : nullptr;
        check(count && count->isVisibleTo(inspector) && count->text().contains("computed per frame"),
              "a scene's inspector says its triangles, and those computed");
        if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); inspector && !captures.isEmpty())
            inspector->grab().save(captures + "/scene-inspector.png");
    }
    check(inspector && inspector->findChild<QComboBox *>("sceneEnvironmentLayer") && inspector->findChild<QComboBox *>("sceneEnvironmentStage"),
          "its Scene tab chooses its environment (an HDRI's layer) and its stage");
    if (inspector) {
        auto *pick = inspector->findChild<QComboBox *>("sceneEnvironmentLayer");
        auto *strength = inspector->findChild<SliderField *>("param3d:scene/environment/intensity");
        check(pick && strength && pick->parentWidget() == strength->parentWidget(), "its choice and its settings in one box");
    }
    for (const char *path : {"param3d:scene/glow", "param3d:scene/glow_threshold", "param3d:scene/vignette", "param3d:scene/saturation", "param3d:scene/fog/density"})
        check(inspector && inspector->findChild<SliderField *>(path), "its look and fog are bars of its Scene tab");
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
    check(objectTabs.contains("Source") && objectTabs.contains("Material") && objectTabs.contains("Spatial") && objectTabs.contains("FX") && !objectTabs.contains("3D"),
          "a model's inspector shows its 3D values, not a picture's tabs");
    check(inspector && inspector->findChild<SliderField *>("param3d:model/scale/all") &&
              inspector->findChild<SliderField *>("param3d:model/scale/x") && inspector->findChild<SliderField *>("param3d:model/scale/z"),
          "the model's scale, overall and along each axis, are bars");
    if (auto *shading = inspector ? inspector->findChild<QComboBox *>("param3d:model/shading") : nullptr) {
        check(shading->findData("smooth") >= 0 && shading->findData("flat") >= 0, "a model's 3D tab chooses its shading");
        const int smooth = shading->findData("smooth");
        shading->setCurrentIndex(smooth);
        emit shading->activated(smooth);
        check(engine.parameterValue(layer, "model/shading").toString() == "smooth", "its choice is the model's");
        if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) {
            for (auto *tabs : inspector->findChildren<QTabWidget *>("layerCategoryTabs"))
                for (int k = 0; k < tabs->count(); ++k)
                    if (tabs->tabText(k).startsWith("3D")) tabs->setCurrentIndex(k);
            pump();
            inspector->grab().save(captures + "/model-inspector.png");
        }
        if (undo) undo->undo();
        check(engine.parameterValue(layer, "model/shading").toString() != "smooth", "undo puts it back");
    } else {
        check(false, "a model's 3D tab has its Shading");
    }
    check(inspector && inspector->findChild<QComboBox *>("modelPictureLayer"), "a model's tab chooses the layer shown on it");
    // Its 3D FX: none by default, as a video layer's; Displace joins the list, and chooses the layer that displaces it
    check(inspector && inspector->findChild<FxChainPanel *>("fxChainPanel") && inspector->findChild<FxChainPanel *>("fxChainPanel")->count() == 0 &&
              !inspector->findChild<QComboBox *>("modelDisplaceLayer"),
          "a model has no 3D FX by default");
    engine.setParameter(layer, fxPath(engine, layer, Fx3d::Displace, "enabled"), true); // turning an FX on lists it
    select(engine.indexOfId(plain));
    select(engine.indexOfId(layer));
    check(inspector && inspector->findChild<FxChainPanel *>("fxChainPanel") && inspector->findChild<FxChainPanel *>("fxChainPanel")->count() == 1,
          "turning Displace on lists it");
    check(inspector && inspector->findChild<QComboBox *>("modelDisplaceLayer"), "the listed Displace chooses the layer that displaces the model");
    check(inspector && inspector->findChild<QComboBox *>("modelPictureStage") && inspector->findChild<QComboBox *>("modelDisplaceStage"),
          "and the stage each one is taken at");
    {   // The list: a second FX of the same kind, then the order, a row's switch and the removal; each an undo step
        const auto kinds = [&] {
            std::vector<Fx3d> out;
            Engine::Lock lk(&engine.mutex());
            for (const Fx3dEntry &e : engine.layer(engine.indexOfId(layer))->fx3d) out.push_back(e.kind);
            return out;
        };
        const auto idOf = [&](int row) {
            Engine::Lock lk(&engine.mutex());
            return engine.layer(engine.indexOfId(layer))->fx3d[size_t(row)].id;
        };
        const auto on = [&](quint64 id) { return engine.parameterValue(layer, QStringLiteral("fx3d/%1/enabled").arg(id)).toBool(); };
        const auto chain = [&] { return inspector ? inspector->findChild<FxChainPanel *>("fxChainPanel") : nullptr; };
        const auto reopen = [&] {
            select(engine.indexOfId(plain));
            select(engine.indexOfId(layer));
        };
        const quint64 bendA = engine.addFx3d(engine.indexOfId(layer), Fx3d::Bend);
        const quint64 bendB = engine.addFx3d(engine.indexOfId(layer), Fx3d::Bend); // the same kind again
        reopen();
        check(kinds() == std::vector<Fx3d>({Fx3d::Displace, Fx3d::Bend, Fx3d::Bend}) && chain() && chain()->count() == 3,
              "the same FX can be listed twice");
        if (chain()) emit chain()->moveRequested(1, 2);
        pump();
        check(idOf(1) == bendB && idOf(2) == bendA, "▲ ▼ change the order they apply in");
        if (undo) undo->undo();
        pump(); // the list is built again
        check(idOf(1) == bendA && idOf(2) == bendB, "undo puts the order back");
        if (chain()) emit chain()->toggled(2, false);
        pump();
        check(!on(bendB) && on(bendA) && kinds().size() == 3, "a row's box switches that one, which stays listed");
        if (undo) undo->undo();
        pump();
        check(on(bendB), "undo switches it back on");
        engine.setParameter(layer, QStringLiteral("fx3d/%1/amount").arg(bendB), 77.0);
        if (chain()) emit chain()->removeRequested(2);
        pump();
        check(kinds() == std::vector<Fx3d>({Fx3d::Displace, Fx3d::Bend}), "− takes the selected one out of the list");
        if (undo) undo->undo();
        pump();
        check(kinds().size() == 3 && std::abs(engine.parameterValue(layer, QStringLiteral("fx3d/%1/amount").arg(bendB)).toDouble() - 77) < 1e-6,
              "undo lists it again, with its settings");
        // The menu keeps every kind, whatever the list holds
        if (chain()) {
            QWidget *anchor = chain()->addButton();
            QTimer::singleShot(100, [&] {
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (auto *menu = qobject_cast<QMenu *>(w); menu && menu->isVisible()) {
                        int kindsInMenu = 0;
                        for (QAction *action : menu->actions())
                            if (!action->isSeparator() && !action->text().isEmpty()) ++kindsInMenu;
                        check(kindsInMenu == 9, "the menu offers every kind of 3D FX, the ones already listed too");
                        menu->close();
                    }
            });
            emit chain()->addRequested(anchor);
        }
        engine.removeFx3d(engine.indexOfId(layer), bendB);
        engine.removeFx3d(engine.indexOfId(layer), bendA);
        reopen();
    }
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
        if (item) {
            QMenu loadMenu;
            bin->fillLoadMenu(&loadMenu, cube);
            bool newScene = false, targets = false;
            for (auto *action : loadMenu.actions()) {
                newScene |= action->text() == "Load into New 3D Scene";
                check(action->text() != "Load into Selected Layer" && action->text() != "Load into New Layer", "3D context has no video loading actions");
                if (!action->menu() || action->text() != "Load into 3D Scene / Merge 3D") continue;
                targets = true;
                for (auto *target : action->menu()->actions()) {
                    bool holder = false;
                    for (int i = 0; i < engine.layerCount(); ++i) {
                        const Layer *l = engine.layer(i);
                        if (l && l->name == target->text().trimmed() && (l->scene3d || l->merge3d)) holder = true;
                    }
                    check(holder, "3D Load into only offers scene and Merge holders");
                }
            }
            check(newScene && targets, "model context offers a new scene and existing 3D holders");
        }

        // A Camera, an Empty and a Path are 3D objects too: a new scene or an existing 3D scene, never a video or a 2D layer
        for (const QString &object : {kCamera3dPath, kEmpty3dPath, kPath3dPath}) {
            QMenu objectMenu;
            bin->fillLoadMenu(&objectMenu, object);
            bool newScene = false, targets = false;
            for (auto *action : objectMenu.actions()) {
                newScene |= action->text() == "Load into New 3D Scene";
                check(action->text() != "Load into Selected Layer" && action->text() != "Load into New Layer" && action->text() != "Load into Layer",
                      "a Camera, an Empty and a Path have no 2D loading actions");
                if (!action->menu() || action->text() != "Load into 3D Scene / Merge 3D") continue;
                targets = true;
                for (auto *target : action->menu()->actions()) {
                    bool holder = false;
                    for (int i = 0; i < engine.layerCount(); ++i) {
                        const Layer *l = engine.layer(i);
                        if (l && l->name == target->text().trimmed() && (l->scene3d || l->merge3d)) holder = true;
                    }
                    check(holder, "a Camera, an Empty or a Path is only offered to scenes and Merge 3D");
                }
            }
            check(newScene && targets, "a Camera, an Empty and a Path offer a new scene and the existing 3D scenes");
        }
        {   // onto a layer that is not a 3D scene (a video, a generator): refused, nothing created
            const int before = engine.layerCount();
            const int plain = engine.addLayer(QStringLiteral("Plain layer"), 0);
            emit bin->loadIntoLayerRequested(plain, kCamera3dPath); // the right-click submenu's signal
            pump();
            check(engine.layerCount() == before + 1, "refusing a Camera onto a 2D layer creates nothing");
            check(engine.layer(plain) && engine.layer(plain)->name == "Plain layer" && engine.layer(plain)->type == SourceType::None,
                  "a Camera is not loaded into a 2D layer");
            engine.removeLayer(plain);
        }
        {   // Load into New 3D Scene: a scene with a light and the object, not an empty 2D layer
            const int before = engine.layerCount();
            emit bin->loadIntoNewLayerRequested(kEmpty3dPath); // the right-click menu's signal
            pump();
            bool scene = false, empty = false;
            for (int i = 0; i < engine.layerCount(); ++i) {
                const Layer *l = engine.layer(i);
                scene |= l && l->scene3d;
                empty |= l && l->merge3d && l->name.startsWith("Empty");
            }
            check(scene && empty && engine.layerCount() > before, "Load into New 3D Scene makes a scene around an Empty");
        }

        QStringList primitives;
        for (QTreeWidget *tree : bin->findChildren<QTreeWidget *>())
            for (QTreeWidgetItem *found : tree->findItems("3D Primitives", Qt::MatchStartsWith | Qt::MatchRecursive))
                for (int k = 0; k < found->childCount(); ++k) primitives << found->child(k)->text(0);
        primitives.sort();
        if (primitives != QStringList({"Camera", "Empty", "Path", "Primitive 3D"}))
            std::printf("3D Primitives: %s\n", qPrintable(primitives.join(", ")));
        check(primitives == QStringList({"Camera", "Empty", "Path", "Primitive 3D"}),
              "the Media Bin offers one configurable 3D primitive, a Camera, an Empty and a Path");
        check(models && models->parent() && models->parent()->text(0) == "3D", "models are separate from video in the Media Bin");
        // Images show their picture in the list, read off the UI thread
        {
            QTemporaryDir pictures;
            QImage wide(400, 200, QImage::Format_RGB32);
            for (int y = 0; y < wide.height(); ++y)
                for (int x = 0; x < wide.width(); ++x) wide.setPixel(x, y, qRgb(x * 255 / wide.width(), y * 255 / wide.height(), 128));
            const QString path = pictures.path() + "/gradient.png";
            check(wide.save(path), "write a picture for the Media Bin");
            bin->importFiles({path});
            QTreeWidgetItem *row = nullptr;
            for (int round = 0; round < 200 && !(row && !row->icon(0).isNull()); ++round) {
                pump();
                QThread::msleep(10);
                for (QTreeWidget *tree : bin->findChildren<QTreeWidget *>())
                    for (QTreeWidgetItemIterator it(tree); *it; ++it)
                        if ((*it)->text(0) == "gradient.png") row = *it;
            }
            check(row && !row->icon(0).isNull(), "an image in the Media Bin shows its picture");
            if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) bin->grab().save(captures + "/media-bin-thumbnail.png");
            engine.removeBinItem(path);
            bin->refresh();
        }
        // The media buttons at the top: coloured, one at a time, each with its count; 3D shows only the 3D media
        auto *pill3d = bin->findChild<QToolButton *>("typePill3D");
        auto *pillVideo = bin->findChild<QToolButton *>("typePillVideo");
        auto *pillAll = bin->findChild<QToolButton *>("typePillAll");
        check(pill3d && pillVideo && bin->findChild<QToolButton *>("typePillAudio") && pillAll, "the Media Bin has All, Video, Audio and 3D buttons");
        if (pill3d && pillVideo && pillAll) {
            pillAll->click(); // All takes every kind away; 3D alone is then chosen
            pill3d->click();
            pump();
            bool videoShown = false, modelsShown = false;
            for (QTreeWidget *tree : bin->findChildren<QTreeWidget *>()) {
                for (QTreeWidgetItem *found : tree->findItems("Video", Qt::MatchStartsWith | Qt::MatchRecursive))
                    if (!found->parent()) videoShown |= !found->isHidden();
                for (QTreeWidgetItem *found : tree->findItems("3D", Qt::MatchStartsWith | Qt::MatchRecursive))
                    if (!found->parent()) modelsShown |= !found->isHidden();
            }
            check(modelsShown && !videoShown && pill3d->isChecked() && !pillAll->isChecked(), "the 3D button shows the 3D media only");
            check(pill3d->text().startsWith("3D "), "and counts them");
            if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) bin->grab().save(captures + "/media-bin.png");
            pillAll->click();
            pump();
        }
    } else {
        check(false, "the Media Bin");
    }
    {
        QUndoStack primitiveUndo;
        LayerInspector inspector(&engine, &primitiveUndo);
        inspector.setLayer(engine.indexOfId(plain));
        inspector.rebuild();
        auto *shape = inspector.findChild<QComboBox *>("primitive3dShape");
        check(shape && shape->count() == Model3D::primitives().size(), "Source offers every primitive shape");
        engine.setParameter(plain, "model/position/x", 2.0);
        const quint64 parent = parentOf(plain);
        if (shape) {
            const int cubeIndex = shape->findData(Model3D::primitivePath("Cube"));
            shape->setCurrentIndex(cubeIndex);
            emit shape->activated(cubeIndex);
            pump();
            check(engine.layer(engine.indexOfId(plain))->sourcePath == Model3D::primitivePath("Cube"), "Source changes primitive shape");
            check(parentOf(plain) == parent && engine.parameterValue(plain, "model/position/x").toDouble() == 2.0, "shape change preserves parent and placement");
            primitiveUndo.undo();
            pump();
            check(engine.layer(engine.indexOfId(plain))->sourcePath == Model3D::primitivePath("Sphere"), "primitive shape change undoes");
            engine.setParameter(plain, "model/position/x", 0.0);
        }
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
            const QImage shot = view->grab().toImage();
            const QColor middle = shot.pixelColor(shot.width() / 2, shot.height() / 2);
            check(middle.lightness() > 60, "the 3D view draws the scene's model");
            // Its button: simple or unlit, saved with the view
            auto *lookButton = view->findChild<QToolButton *>("scene3dLook");
            check(lookButton && lookButton->menu() && lookButton->menu()->actions().size() == 2 && lookButton->text() == "Simple",
                  "the 3D view chooses how it draws, simple at first");
            if (lookButton && lookButton->menu()) {
                for (QAction *action : lookButton->menu()->actions())
                    if (action->text() == "Unlit") action->trigger();
                for (int k = 0; k < 3; ++k) {
                    engine.renderFrame();
                    pump();
                }
                const QImage unlit = view->grab().toImage();
                check(view->look() == Scene3D::ViewLook::Unlit && lookButton->text() == "Unlit", "unlit, chosen");
                check(unlit != shot, "it draws differently");
                check(view->viewState().value("look").toString() == "unlit", "saved with the view");
                if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) {
                    shot.save(captures + "/view-simple.png");
                    unlit.save(captures + "/view-unlit.png");
                }
                QJsonObject state = view->viewState();
                state["look"] = "simple";
                view->setViewState(state);
                check(view->look() == Scene3D::ViewLook::Simple && lookButton->text() == "Simple", "read back");
            }
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
            // H: all the scene holds, seen whole
            {
                engine.setParameter(layer, "model/position/x", 10.0);
                pump();
                QKeyEvent press(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier);
                QApplication::sendEvent(view, &press);
                pump();
                const QRectF seen(0, 0, view->width(), view->height());
                check(seen.contains(view->project({10, 0, 0})) && seen.contains(view->project({0, 0, 0})), "H: what is far away is seen");
                engine.setParameter(layer, "model/position/x", 0.0);
                view->setCamera(cam);
                check(view->findChild<QToolButton *>("scene3dHome") != nullptr, "and a button for it");
                pump();
            }
            // The three kinds of light are drawn differently, a spot with its cone
            {
                const int at = engine.addLayer("Drawn light", engine.indexOfId(scene) + 1);
                const quint64 drawn = engine.layerId(at);
                check(engine.setLayerLight(at), "a light to draw");
                engine.setStructure(tree::intoGroup(engine.structure(), {drawn}, scene));
                engine.setParameter(drawn, "light/position/x", 1.5);
                engine.setParameter(drawn, "light/position/y", 2.0);
                engine.setParameter(drawn, "light/position/z", 0.0);
                engine.setParameter(drawn, "light/pitch", -80.0);
                engine.setParameter(drawn, "light/intensity", 0.0); // only its symbol differs
                // Its settings are in the inspector
                {
                    QUndoStack inspectorUndo;
                    LayerInspector inspector(&engine, &inspectorUndo);
                    inspector.setLayer(engine.indexOfId(drawn));
                    inspector.rebuild();
                    pump();
                    QStringList shown;
                    for (auto *label : inspector.findChildren<QLabel *>()) shown << label->text();
                    for (const char *name : {"Spot Falloff", "Shadows", "Shadow Softness"})
                        check(shown.filter(QLatin1String(name)).size() > 0, "a light's setting in the inspector");
                    // What it looks at, and the Path it goes along (one of the Paths of its scene, chosen as what it looks at is)
                    auto *follow = inspector.findChild<QComboBox *>("lightLookAtObject");
                    auto *pathPick = inspector.findChild<QComboBox *>("lightPath");
                    check(inspector.findChild<QCheckBox *>("param3d:light/lookat") && inspector.findChild<SliderField *>("param3d:light/lookpoint/x"),
                          "a light's inspector has its Look At");
                    check(pathPick && pathPick->itemText(0) == "None" && !inspector.findChild<QComboBox *>("param3d:light/path/shape"),
                          "a light's inspector chooses a Path, not a drawn circle or rectangle");
                    {   // Choosing a Path: the light gets its Follow Path FX (one undo step); None takes it away
                        const quint64 scene3d = parentOf(drawn);
                        const int trackAt = engine.addLayer("Track", engine.indexOfId(scene3d) + 1);
                        const quint64 trackId = engine.layerId(trackAt);
                        engine.setLayerPath(trackAt);
                        engine.setStructure(tree::intoGroup(engine.structure(), {trackId}, scene3d));
                        inspector.setLayer(engine.indexOfId(drawn)); // the rows moved
                        inspector.rebuild();
                        pump();
                        auto *pick = inspector.findChild<QComboBox *>("lightPath");
                        const int row = pick ? pick->findData(QVariant(qulonglong(trackId))) : -1;
                        check(row > 0, "a Path of the scene can be chosen for the light");
                        const auto followed = [&] {
                            Engine::Lock lk(&engine.mutex());
                            const Fx3dEntry *e = engine.layer(engine.indexOfId(drawn))->fx3dFirst(Fx3d::FollowPath);
                            return e ? e->s.layer : quint64(0);
                        };
                        if (row > 0) {
                            const int undoBefore = inspectorUndo.count();
                            emit pick->activated(row);
                            pump();
                            check(followed() == trackId && inspectorUndo.count() == undoBefore + 1, "the light follows it, as one step");
                            inspector.rebuild();
                            pump();
                            check(inspector.findChild<SliderField *>(QStringLiteral("param3d:fx3d/%1/speed").arg(engine.fx3dOfKind(engine.indexOfId(drawn), Fx3d::FollowPath))),
                                  "its position and speed on the Path are there");
                            inspectorUndo.undo();
                            pump();
                            check(followed() == 0, "undo: no Path again");
                            inspectorUndo.redo();
                            pump();
                            inspector.rebuild();
                            pump();
                            if (auto *again = inspector.findChild<QComboBox *>("lightPath")) {
                                emit again->activated(0);
                                pump();
                                check(!engine.fx3dOfKind(engine.indexOfId(drawn), Fx3d::FollowPath), "None: the Follow Path goes");
                            }
                        }
                        engine.removeLayer(engine.indexOfId(trackId));
                        inspector.setLayer(engine.indexOfId(drawn));
                        inspector.rebuild();
                        pump();
                    }
                    follow = inspector.findChild<QComboBox *>("lightLookAtObject"); // the inspector was built again
                    const int cubeThere = follow ? follow->findData(QVariant(qulonglong(layer))) : -1;
                    check(cubeThere > 0, "it can look at the scene's other objects");
                    if (cubeThere > 0) {
                        const int undoMark = inspectorUndo.count();
                        emit follow->activated(cubeThere);
                        const auto looksAt = [&] {
                            Engine::Lock lk(&engine.mutex());
                            const Layer *l = engine.layer(engine.indexOfId(drawn));
                            return l ? l->lightLookAt : 0;
                        };
                        check(looksAt() == layer, "choosing an object: the light looks at it");
                        check(inspectorUndo.count() == undoMark + 1, "one undo step");
                        inspectorUndo.undo();
                        check(looksAt() == 0, "undo: it does not any more");
                    }
                    if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty())
                        inspector.grab().save(captures + "/light-inspector.png");
                }
                view->setSelected(0);
                const auto lit = [&](const QString &kind) {
                    engine.setParameter(drawn, "light/type", kind);
                    for (int k = 0; k < 3; ++k) {
                        engine.renderFrame();
                        pump();
                    }
                    return view->grab().toImage();
                };
                const QImage point = lit("point"), spot = lit("spot"), sun = lit("directional"), panel = lit("area");
                const auto different = [](const QImage &x, const QImage &y) {
                    int count = 0;
                    for (int j = 0; j < x.height(); ++j)
                        for (int i = 0; i < x.width(); ++i) count += x.pixel(i, j) != y.pixel(i, j);
                    return count;
                };
                check(different(point, spot) > 200, "a spot is drawn with its cone");
                check(different(point, sun) > 50 && different(sun, spot) > 200, "a directional light has its own symbol");
                check(different(point, panel) > 30 && different(spot, panel) > 100, "an area light is drawn as its rectangle");
                engine.setParameter(drawn, "light/type", QStringLiteral("spot"));
                engine.setParameter(drawn, "light/falloff", 0.0);
                for (int k = 0; k < 3; ++k) {
                    engine.renderFrame();
                    pump();
                }
                const QImage sharp = view->grab().toImage();
                check(different(sharp, spot) > 20, "its falloff is drawn inside its cone");
                // Its path and what it looks at are drawn too, each on its own, from far enough to see them
                {
                    SceneSettings wide;
                    wide.distance = 12;
                    wide.yaw = 30;
                    wide.pitch = 30;
                    view->setCamera(wide);
                    engine.setParameter(drawn, "light/type", QStringLiteral("point"));
                    const auto shot = [&] {
                        for (int k = 0; k < 3; ++k) {
                            engine.renderFrame();
                            pump();
                        }
                        return view->grab().toImage();
                    };
                    const QImage bare = shot();
                    engine.setParameter(drawn, "light/lookat", true);
                    const QImage looking = shot();
                    engine.setParameter(drawn, "light/lookat", false);
                    check(different(bare, looking) > 50, "what a light looks at shows in the 3D view");
                    if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) {
                        looking.save(captures + "/light-looking.png");
                    }
                    view->setCamera(cam);
                    engine.setParameter(drawn, "light/type", QStringLiteral("spot"));
                }
                if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) {
                    point.save(captures + "/light-point.png");
                    spot.save(captures + "/light-spot.png");
                    sun.save(captures + "/light-sun.png");
                }
                // New Merge 3D: the 3D object selected goes into it, in its place; it has its own 3D tab
                emit view->layerPicked(engine.indexOfId(drawn)); // as a click on it
                pump();
                const int layersBefore = engine.layerCount();
                QAction *newMerge = nullptr;
                for (QAction *action : window.findChildren<QAction *>())
                    if (action->text().startsWith("New Merge 3D")) newMerge = action;
                check(newMerge != nullptr, "the menu offers a Merge 3D");
                if (newMerge) newMerge->trigger();
                pump();
                check(engine.layerCount() == layersBefore + 1, "New Merge 3D makes one");
                quint64 merged = 0;
                {
                    Engine::Lock lk(&engine.mutex());
                    if (const Layer *l = engine.layer(engine.indexOfId(drawn))) merged = l->parent;
                }
                {
                    Engine::Lock lk(&engine.mutex());
                    const Layer *m = engine.layer(engine.indexOfId(merged));
                    check(m && m->merge3d && m->parent == scene, "the object is in it, and it is in the scene");
                }
                check(tree::valid3d(engine.structure()), "the 3D rule holds");
                if (merged) {
                    QUndoStack inspectorUndo;
                    LayerInspector inspector(&engine, &inspectorUndo);
                    inspector.setLayer(engine.indexOfId(merged));
                    inspector.rebuild();
                    pump();
                    QStringList shown;
                    for (auto *label : inspector.findChildren<QLabel *>()) shown << label->text();
                    check(!shown.filter("Position X").isEmpty() && !shown.filter("Rotation Y").isEmpty(), "a Merge 3D's transform in the inspector");
                    check(!shown.filter("Merge 3D").isEmpty() && shown.filter("Blend Mode").isEmpty(), "it says what it is: no picture, no blending");
                    if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) {
                        engine.setParameter(merged, "merge/position/x", -1.0);
                        emit view->layerPicked(engine.indexOfId(merged));
                        view->setSelected(merged);
                        for (int k = 0; k < 4; ++k) {
                            engine.renderFrame();
                            pump();
                        }
                        view->grab().toImage().save(captures + "/merge3d-view.png");
                        inspector.resize(420, 620);
                        inspector.grab().save(captures + "/merge3d-inspector.png");
                        if (auto *box = window.findChild<QWidget *>("quality3d")) box->grab().save(captures + "/quality3d.png");
                    }
                    // Its handles: the view knows it, and moves it
                    engine.setParameter(merged, "merge/position/x", 0.3);
                    pump();
                    check(std::abs(engine.parameterValue(drawn, "light/position/x").toDouble() - 1.5) < 1e-6, "what it holds keeps its own values");
                    engine.setParameter(merged, "merge/position/x", 0.0);
                }
                // The composition's quality, for all the scenes
                check(window.findChild<QComboBox *>("quality3d/glow") && window.findChild<QComboBox *>("quality3d/atmosphere"), "the 3D quality has its glow and fog");
                auto *shadows = window.findChild<QComboBox *>("quality3d/shadows");
                check(shadows != nullptr, "the composition offers the 3D quality");
                if (shadows) {
                    shadows->setCurrentText("low");
                    check(engine.quality3d().shadows == 1, "it sets the shadows of all the scenes");
                    shadows->setCurrentText("medium");
                }
                if (merged) engine.removeLayer(engine.indexOfId(merged));
                else engine.removeLayer(engine.indexOfId(drawn));
                pump();
            }
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
                view->grab().toImage().save(captures + "/scene3d-view.png");
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
    engine.newProject();
    engine.setCompositionSize({480, 270});
    engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {480, 270});
    const auto materialScene = engine.layerId(engine.addScene("Shared material UI"));
    const auto materialA = engine.layerId(engine.addLayer("Material A"));
    const auto materialB = engine.layerId(engine.addLayer("Material B"));
    QString materialError;
    check(engine.setLayerModel(engine.indexOfId(materialA), Model3D::primitivePath("Cube"), &materialError), "create first material object");
    check(engine.setLayerModel(engine.indexOfId(materialB), Model3D::primitivePath("Sphere"), &materialError), "create second material object");
    engine.setStructure(tree::intoGroup(engine.structure(), {materialA, materialB}, materialScene));
    check(engine.setModelMaterial(engine.indexOfId(materialB), engine.addMaterial("Independent surface")), "explicit material overrides scene default");
    quint64 materialId = 0, originalMaterial = 0;
    {
        Engine::Lock lock(&engine.mutex());
        materialId = engine.layer(engine.indexOfId(materialA))->modelMaterial;
        originalMaterial = engine.layer(engine.indexOfId(materialB))->modelMaterial;
    }
    select(engine.indexOfId(materialB)); pump();
    LayerInspector *materialInspector = nullptr;
    for (auto *candidate : window.findChildren<LayerInspector *>())
        if (candidate->layerId() == materialB) materialInspector = candidate;
    if (materialInspector) { materialInspector->rebuild(); pump(); }
    auto *materialMenu = materialInspector ? materialInspector->findChild<QComboBox *>("modelMaterial") : nullptr;
    if (!materialMenu || materialMenu->findData(QString::number(materialId)) < 0)
        std::printf("material UI diagnostics: target=%llu inspector=%llu menu=%d choices=%d material=%llu resources=%zu scene=%d\n",
            (unsigned long long)materialB, (unsigned long long)(materialInspector ? materialInspector->layerId() : 0),
            int(materialMenu != nullptr), materialMenu ? materialMenu->count() : 0, (unsigned long long)materialId,
            engine.materials().size(), engine.sceneOfObject(engine.indexOfId(materialB)));
    check(materialMenu && materialMenu->findData(QString::number(materialId)) >= 0, "material selector lists project resources");
    if (materialMenu) {
        const int choice = materialMenu->findData(QString::number(materialId));
        materialMenu->setCurrentIndex(choice);
        emit materialMenu->activated(choice); pump();
        check(engine.setParameter(materialA, "model/material/color/r", .15), "edit shared material from first object");
        check(std::abs(engine.parameterValue(materialB, "model/material/color/r").toDouble() - .15) < 1e-6, "assigned material is shared through the inspector");
        undo->undo(); pump();
        { Engine::Lock lock(&engine.mutex()); check(engine.layer(engine.indexOfId(materialB))->modelMaterial == originalMaterial, "material assignment undo"); }
        undo->redo(); pump();
        { Engine::Lock lock(&engine.mutex()); check(engine.layer(engine.indexOfId(materialB))->modelMaterial == materialId, "material assignment redo"); }
    }
    if (materialInspector) {
        {
            Engine::Lock lock(&engine.mutex());
            QImage normal(2, 2, QImage::Format_RGBA8888);
            normal.fill(QColor(128, 128, 255));
            check(engine.materials().at(materialId)->setEmbeddedMap(NormalMap, normal, QStringLiteral("normal-source.png")),
                  "set a named embedded map");
        }
        materialInspector->rebuild();
        pump();
        const auto *map = materialInspector->findChild<QPushButton *>("materialMap_normal");
        check(map && map->text() == "normal-source.png", "the object shows the source image name");
        auto *strength = materialInspector->findChild<SliderField *>("Normal Strength");
        check(strength != nullptr, "the object exposes normal strength");
        if (strength) {
            emit strength->valueEdited(2.0);
            check(engine.materials().at(materialId)->normalStrength == 2.f, "object edits the shared project material");
            undo->undo();
            check(engine.materials().at(materialId)->normalStrength == 1.f, "object material edit undoes");
        }
    }
    window.hide();
    {
        QUndoStack materialUndo;
        MaterialPanel materials(&engine, &materialUndo, [&] { return engine.indexOfId(materialB); }, [] {});
        materials.resize(380, 900);
        materials.show();
        pump();
        auto *add = materials.findChild<QPushButton *>("addMaterial");
        auto *items = materials.findChild<QListWidget *>("materialList");
        const int count = int(engine.materials().size());
        check(add && items, "material browser exposes creation and list");
        if (add && items) {
            add->click(); pump();
            check(int(engine.materials().size()) == count + 1, "material browser creates project material");
            const quint64 id = items->currentItem()->data(Qt::UserRole).toULongLong();
            auto *metal = materials.findChild<SliderField *>("materialMetal");
            auto *preview = materials.findChild<QOpenGLWidget *>("materialSphere");
            auto *color = materials.findChild<ColorPicker *>("materialBaseColor");
            const QImage before = preview ? preview->grab().toImage() : QImage();
            if (metal) emit metal->valueEdited(.9);
            if (color) emit color->colorEdited(QColor(Qt::red));
            pump();
            const QImage after = preview ? preview->grab().toImage() : QImage();
            check(engine.materials().at(id)->metal > .89f && engine.materials().at(id)->color[1] == 0, "material controls edit shared resource");
            check(!before.isNull() && !after.isNull() && before != after, "sphere preview responds to material changes");
            if (const QString file = qEnvironmentVariable("NYXMAP_MATERIAL_CAPTURE"); !file.isEmpty()) materials.grab().save(file);
            materialUndo.undo(); pump();
            check(engine.materials().at(id)->color[1] > .7f, "material color edit undoes");
            auto *assign = materials.findChild<QPushButton *>("assignMaterial");
            if (assign) assign->click();
            pump();
            check(engine.layer(engine.indexOfId(materialB))->modelMaterial == id, "browser assigns material to selected object");
            auto *remove = materials.findChild<QPushButton *>("removeMaterial");
            check(remove && !remove->isEnabled(), "used material cannot be removed");
            materialUndo.undo(); pump();
            check(engine.layer(engine.indexOfId(materialB))->modelMaterial != id, "material assignment undoes");
        }
        materials.hide();
    }
    return failures;
}
