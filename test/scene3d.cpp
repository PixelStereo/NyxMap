#include "Engine.h"
#include "Model3D.h"
#include <QColor>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <cstdio>

// 3D scenes (Model3D.h): a group whose picture its models make, seen by its camera, under its lights
int runScene3dTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL scene3d: %s\n", why); }
    };
    QTemporaryDir dir;
    // A unit cube (OBJ, with its normals) and a triangle (glTF, its buffer inline, no normals: flat ones are made)
    const QString cube = dir.path() + "/cube.obj", triangle = dir.path() + "/triangle.gltf";
    {
        QFile f(cube);
        check(f.open(QIODevice::WriteOnly), "write the cube");
        f.write("v -0.5 -0.5 -0.5\nv 0.5 -0.5 -0.5\nv 0.5 0.5 -0.5\nv -0.5 0.5 -0.5\n"
                "v -0.5 -0.5 0.5\nv 0.5 -0.5 0.5\nv 0.5 0.5 0.5\nv -0.5 0.5 0.5\n"
                "vn 0 0 -1\nvn 0 0 1\nvn -1 0 0\nvn 1 0 0\nvn 0 -1 0\nvn 0 1 0\n"
                "f 1//1 3//1 2//1\nf 1//1 4//1 3//1\nf 5//2 6//2 7//2\nf 5//2 7//2 8//2\n"
                "f 1//3 5//3 8//3\nf 1//3 8//3 4//3\nf 2//4 3//4 7//4\nf 2//4 7//4 6//4\n"
                "f 1//5 2//5 6//5\nf 1//5 6//5 5//5\nf 4//6 8//6 7//6\nf 4//6 7//6 3//6\n");
    }
    {
        QFile f(triangle);
        check(f.open(QIODevice::WriteOnly), "write the triangle");
        // three float32 vec3: (0,0,0) (1,0,0) (0,1,0)
        const QByteArray data = QByteArray::fromHex("000000000000000000000000" "0000803f0000000000000000" "000000000000803f00000000");
        f.write(QStringLiteral(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
"buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,%1"}],
"bufferViews":[{"buffer":0,"byteLength":36}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}]})")
                    .arg(QString::fromLatin1(data.toBase64()))
                    .toUtf8());
    }
    Model3D gltf;
    QString error;
    check(gltf.load(triangle, &error) && gltf.meshCount() == 1 && gltf.triangleCount() == 1, "a glTF is read (inline buffer, no normals)");

    // The built-in primitives: each one read, unwrapped, about a unit in size
    for (const QString &name : Model3D::primitives()) {
        Model3D shape;
        const QString path = Model3D::primitivePath(name);
        const bool ok = Model3D::isModelFile(path) && Model3D::isPrimitivePath(path) && shape.load(path, &error) &&
                        shape.meshCount() == 1 && shape.triangleCount() >= 2;
        if (!ok) std::printf("FAIL scene3d: built-in primitive %s\n", qPrintable(name)), ++failures;
    }
    check(!Model3D::isPrimitivePath("/NyxMap/3D/Teapot"), "an unknown primitive is not one");

    engine.newProject();
    engine.setCompositionSize({64, 64});
    engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {64, 64});
    const int si = engine.addScene();
    const quint64 scene = engine.layerId(si);
    const int mi = engine.addLayer("Cube", si + 1);
    const quint64 model = engine.layerId(mi);
    check(engine.setLayerModel(mi, cube, &error), "the cube is read");
    const int li = engine.addLayer("Key", engine.indexOfId(model) + 1);
    const quint64 light = engine.layerId(li);
    check(engine.setLayerLight(li), "a layer becomes a light");
    engine.setStructure(tree::intoGroup(engine.structure(), {model, light}, scene));
    const LayerTree t = engine.structure();
    check(t[size_t(tree::indexOf(t, scene))].role == TreeNode::Scene3d && t[size_t(tree::indexOf(t, model))].role == TreeNode::Object3d &&
              t[size_t(tree::indexOf(t, light))].role == TreeNode::Object3d,
          "the structure knows the scene and its objects");
    check(tree::valid3d(t), "a scene holding a model and a light is valid");
    check(!tree::valid3d(tree::ungrouped(t, scene)), "3D objects out of their scene are refused");
    const int plain = engine.addLayer("Plain");
    check(!tree::valid3d(tree::intoGroup(engine.structure(), {engine.layerId(plain)}, scene)), "a 2D layer in a scene is refused");
    engine.removeLayer(engine.indexOfId(engine.layerId(plain)));

    auto center = [&] {
        engine.renderFrame();
        return engine.grabOutput().pixelColor(32, 32);
    };
    const QColor lit = center();
    // The output is over black: the scene's transparent background shows it
    check(lit.lightness() > 40, "the cube is drawn in the middle, lit");
    engine.renderFrame();
    check(engine.grabOutput().pixelColor(1, 1).lightness() == 0, "around it, the transparent background");
    engine.setParameter(light, "light/intensity", 0.0);
    const QColor dark = center();
    check(dark.lightness() > 0 && dark.lightness() < lit.lightness(), "without its light, only the ambient light");
    engine.setParameter(light, "light/intensity", 25.0);
    engine.setParameter(light, "light/level", 0.0);
    check(center().lightness() < lit.lightness(), "a light's level dims it");
    engine.setParameter(light, "light/level", 1.0);
    engine.setParameter(model, "enable", false);
    check(center().lightness() == 0, "a hidden model is not drawn");
    engine.setParameter(model, "enable", true);
    engine.setParameter(model, "model/position/x", 5.0);
    check(center().lightness() == 0, "a model moved aside leaves the middle");
    engine.setParameter(model, "model/position/x", 0.0);
    engine.setParameter(scene, "scene/background/a", 1.0);
    engine.setParameter(scene, "scene/background/b", 1.0);
    engine.renderFrame();
    const QColor bg = engine.grabOutput().pixelColor(1, 1);
    check(bg.blue() > 200 && bg.red() < 30, "the scene's background color");
    engine.setParameter(scene, "camera/yaw", 60.0);

    // A primitive in the scene: drawn, saved as built in (no file)
    const int pi = engine.addLayer("Torus", engine.indexOfId(light) + 1);
    const quint64 torus = engine.layerId(pi);
    check(engine.setLayerModel(pi, Model3D::primitivePath("Torus"), &error), "a primitive becomes a model");
    engine.setStructure(tree::intoGroup(engine.structure(), {torus}, scene));
    bool listed = false;
    for (const auto &r : engine.mediaUsage()) listed |= Model3D::isPrimitivePath(r.path);
    check(!listed, "a primitive is not a media file");

    // A contact sheet of the primitives, lit, when asked (NYXMAP_UI_CAPTURE_DIR)
    if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) {
        engine.setCompositionSize({240, 240});
        engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {240, 240});
        engine.setParameter(scene, "scene/background/a", 0.0);
        engine.setParameter(model, "enable", false);
        const QStringList names = Model3D::primitives();
        QImage sheet(240 * 4, 240 * ((int(names.size()) + 3) / 4), QImage::Format_RGB32);
        sheet.fill(Qt::black);
        QPainter painter(&sheet);
        for (int k = 0; k < names.size(); ++k) {
            engine.setLayerModel(engine.indexOfId(torus), Model3D::primitivePath(names[k]), &error);
            engine.renderFrame();
            painter.drawImage(240 * (k % 4), 240 * (k / 4), engine.grabOutput());
            painter.setPen(Qt::white);
            painter.drawText(240 * (k % 4) + 6, 240 * (k / 4) + 16, names[k]);
        }
        painter.end();
        sheet.save(captures + "/primitives.png");
        engine.setLayerModel(engine.indexOfId(torus), Model3D::primitivePath("Torus"), &error);
        engine.setParameter(model, "enable", true);
        engine.setCompositionSize({64, 64});
    }

    // Saved and opened again: the scene, its objects and their values
    const QString project = dir.path() + "/scene.nyxmap";
    check(engine.saveProject(project, {}, &error), "save the scene");
    engine.newProject();
    QJsonObject ui;
    check(engine.loadProject(project, &ui, &error), "open the scene");
    engine.setCompositionSize({64, 64});
    const int si2 = engine.indexOfId(scene), mi2 = engine.indexOfId(model), li2 = engine.indexOfId(light);
    bool kept = si2 >= 0 && mi2 >= 0 && li2 >= 0;
    if (kept) {
        Engine::Lock lk(&engine.mutex());
        kept = engine.layer(si2)->scene3d && engine.layer(mi2)->type == SourceType::Model && engine.layer(mi2)->model &&
               engine.layer(li2)->type == SourceType::Light && engine.layer(mi2)->parent == scene;
    }
    check(kept, "the scene, its model and its light come back");
    {
        Engine::Lock lk(&engine.mutex());
        const Layer *t2 = engine.layer(engine.indexOfId(torus));
        check(t2 && t2->type == SourceType::Model && t2->model && t2->sourcePath == Model3D::primitivePath("Torus"),
              "the primitive comes back");
    }
    check(std::abs(engine.parameterValue(scene, "camera/yaw").toDouble() - 60) < 1e-6, "the camera's values come back");
    check(center().lightness() > 40, "the opened scene draws again");
    return failures;
}
