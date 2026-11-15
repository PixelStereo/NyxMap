#include "Engine.h"
#include "MappingView.h"
#include "Model3D.h"
#include "PreviewWorkspace.h"
#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTimer>
#include <cstdio>
#include <functional>
int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setApplicationName("NyxMapPreviewTest");
    Engine engine;
    QString error;
    if (!engine.initialize(&error)) { std::fprintf(stderr, "%s\n", qPrintable(error)); return 1; }
    engine.newProject();
    engine.setCompositionSize(QSize(64, 64));
    const int index = engine.addLayer("Test layer");
    QTemporaryDir directory;
    QImage image(64, 64, QImage::Format_RGBA8888); image.fill(Qt::red);
    const QString path = directory.path() + "/red.png";
    if (!image.save(path) || !engine.setLayerImage(index, path, &error)) return 1;
    int failures = 0;
    auto check = [&](bool ok, const char *message) { if (!ok) { ++failures; std::printf("FAIL preview UI: %s\n", message); } };
    {
        auto *mapping = new MappingView(&engine);
        PreviewWorkspace workspace(&engine, mapping);
        workspace.resize(960, 700);
        workspace.show();
        auto *layout = workspace.findChild<QComboBox *>("previewLayout");
        check(layout != nullptr, "layout selector present");
        auto *source = workspace.findChild<QToolButton *>("previewSource0");
        for (int count = 1; count <= 4; ++count) {
            layout->setCurrentIndex(count - 1);
            engine.renderFrame(); workspace.refreshFrames(); app.processEvents();
            for (int slot = 0; slot < 4; ++slot)
                check(workspace.findChild<QToolButton *>(QString("previewSource%1").arg(slot))->isVisible() == (slot < count), "visible cells follow selected split");
        }
        QMetaObject::invokeMethod(source->menu(), "aboutToShow", Qt::DirectConnection);
        bool selected = false;
        for (auto *action : source->menu()->actions()) if (action->menu() && action->text() == "Layers")
            for (auto *layer : action->menu()->actions()) if (layer->text() == "Test layer") { layer->trigger(); selected = true; }
        check(selected && !mapping->isVisible(), "layer source replaces mapping view");
        engine.renderFrame(); workspace.refreshFrames(); app.processEvents();
        source->menu()->actions().first()->trigger(); app.processEvents();
        check(mapping->isVisible(), "Composition restores mapping editor");
        {   // A 3D scene and a Merge 3D can be looked at, their objects (a model, a light, a Path) cannot
            const int scene = engine.addScene("Stage");
            const quint64 sceneId = engine.layerId(scene);
            const int merge = engine.addMerge3d("Group of things", scene + 1);
            const quint64 mergeId = engine.layerId(merge);
            const int model = engine.addLayer("Box", merge + 1);
            check(engine.setLayerModel(model, Model3D::primitivePath("Cube"), &error), "a model for the viewer menu");
            const int light = engine.addLayer("Lamp", model + 1);
            engine.setLayerLight(light);
            engine.setStructure(tree::intoGroup(engine.structure(), {engine.layerId(merge)}, sceneId));
            engine.setStructure(tree::intoGroup(engine.structure(), {engine.layerId(model)}, mergeId));
            engine.setStructure(tree::intoGroup(engine.structure(), {engine.layerId(light)}, sceneId));
            QMetaObject::invokeMethod(source->menu(), "aboutToShow", Qt::DirectConnection);
            QStringList names;
            std::function<void(QMenu *)> collect = [&](QMenu *menu) {
                for (QAction *action : menu->actions()) {
                    if (action->menu()) collect(action->menu());
                    else if (!action->isSeparator()) names << action->text();
                }
            };
            collect(source->menu());
            check(names.contains("Stage (3D view)"), "the viewer offers a 3D scene");
            check(names.contains("Group of things (3D view)"), "and a Merge 3D");
            check(!names.contains("Box") && !names.contains("Lamp"), "but not the 3D objects of a scene");
            // A Merge 3D shows its own 3D view
            QAction *picked = nullptr;
            std::function<void(QMenu *)> find = [&](QMenu *menu) {
                for (QAction *action : menu->actions()) {
                    if (action->menu()) find(action->menu());
                    else if (action->text() == "Group of things (3D view)") picked = action;
                }
            };
            find(source->menu());
            if (picked) picked->trigger();
            app.processEvents();
            engine.renderFrame(); workspace.refreshFrames(); app.processEvents();
            check(!mapping->isVisible() && source->text() == "Group of things (3D view)", "a Merge 3D is looked at in a 3D view");
            source->menu()->actions().first()->trigger();
            app.processEvents();
        }
        layout->setCurrentIndex(0); app.processEvents();
        check(!workspace.grab().isNull(), "preview renders to a widget image");
        if (argc > 1) check(workspace.grab().save(QString::fromLocal8Bit(argv[1])), "save UI capture");
        workspace.hide();
    }
    engine.shutdown();
    return failures ? 1 : 0;
}
