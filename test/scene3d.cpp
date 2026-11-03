#include "Engine.h"
#include "ImageFile.h"
#include "Model3D.h"
#include <QColor>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <cstdio>
#include <tuple>

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
    // As few triangles as the shape needs: a mesh that costs nothing unless it is asked to
    {
        const auto triangles = [&](const QString &name) {
            Model3D shape;
            return shape.load(Model3D::primitivePath(name), &error) ? shape.triangleCount() : -1;
        };
        check(triangles("Plane") == 2, "a plane is 2 triangles");
        check(triangles("Cube") == 12, "a cube is 12");
        check(triangles("Pyramid") == 6, "a pyramid is 6");
        check(triangles("Sphere") <= 600 && triangles("Cylinder") <= 100 && triangles("Cone") <= 100, "round ones, enough to be round");
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
    engine.renderFrame();
    check(engine.grabOutput().pixelColor(20, 32).lightness() > 0, "the cube reaches aside the middle");
    engine.setParameter(model, "model/scale/x", 0.1);
    engine.renderFrame();
    check(engine.grabOutput().pixelColor(20, 32).lightness() == 0 && engine.grabOutput().pixelColor(32, 32).lightness() > 0,
          "scaled along X only, it narrows and stays in the middle");
    engine.setParameter(model, "model/scale/x", 1.0);
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

    // Surfaces: a layer's picture on a plane (the right way up), unlit; bent; its points pushed by a luminance
    engine.newProject();
    engine.setCompositionSize({64, 64});
    engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {64, 64});
    const QString halves = dir.path() + "/Halves.fs", white = dir.path() + "/White.fs";
    {
        QFile f(halves);
        check(f.open(QIODevice::WriteOnly), "write the halves shader");
        f.write("/*{\"ISFVSN\":\"2\"}*/\nvoid main() { gl_FragColor = isf_FragNormCoord.y > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 0.0, 1.0, 1.0); }\n");
        QFile w(white);
        check(w.open(QIODevice::WriteOnly), "write the white shader");
        w.write("/*{\"ISFVSN\":\"2\"}*/\nvoid main() { gl_FragColor = vec4(1.0); }\n");
    }
    const int pi2 = engine.addLayer("Picture");
    const quint64 picture = engine.layerId(pi2);
    check(engine.setLayerIsf(pi2, halves, &error), "a picture layer: red above, blue below");
    engine.setParameter(picture, "enable", false); // hidden: still drawn for the model that shows it
    const int wi = engine.addLayer("Bump");
    const quint64 bump = engine.layerId(wi);
    check(engine.setLayerIsf(wi, white, &error), "a white layer");
    engine.setParameter(bump, "enable", false);
    const int s2 = engine.addScene();
    const quint64 stage = engine.layerId(s2);
    const int pl = engine.addLayer("Screen", s2 + 1);
    const quint64 screen = engine.layerId(pl);
    check(engine.setLayerModel(pl, Model3D::primitivePath("Plane"), &error), "a plane");
    engine.setStructure(tree::intoGroup(engine.structure(), {screen}, stage));
    engine.setParameter(stage, "camera/yaw", 0.0); // straight in front of it
    engine.setParameter(stage, "camera/pitch", 0.0);
    engine.setParameter(screen, "model/unlit", true);
    check(engine.setModelPicture(engine.indexOfId(screen), picture, &error), "a layer shown on the plane");
    check(!engine.setModelPicture(engine.indexOfId(screen), stage, &error), "its own scene is refused (a loop)");
    auto at = [&](int x, int y) {
        engine.renderFrame();
        return engine.grabOutput().pixelColor(x, y);
    };
    const QColor top = at(32, 26), bottom = at(32, 38);
    check(top.red() > 200 && top.blue() < 40 && bottom.blue() > 200 && bottom.red() < 40,
          "the picture lies the right way up, as it is (unlit)");
    // Any stage of that layer: two FX that invert it, its final picture as it was, after its first FX inverted
    const QString invert = dir.path() + "/Invert.fs";
    {
        QFile f(invert);
        check(f.open(QIODevice::WriteOnly), "write the invert FX");
        f.write("/*{\"ISFVSN\":\"2\",\"INPUTS\":[{\"NAME\":\"inputImage\",\"TYPE\":\"image\"}]}*/\n"
                "void main() { vec4 c = IMG_THIS_NORM_PIXEL(inputImage); gl_FragColor = vec4(1.0 - c.rgb, c.a); }\n");
    }
    check(engine.addEffect(engine.indexOfId(picture), invert, &error) >= 0 && engine.addEffect(engine.indexOfId(picture), invert, &error) >= 0,
          "two inverting FX on the picture layer");
    const QColor final = at(32, 26);
    check(final.red() > 200 && final.blue() < 40, "its final picture: inverted twice, as it was");
    check(engine.setModelLayer(engine.indexOfId(screen), picture, false, &error, 1), "taken after its first FX");
    const QColor afterFirst = at(32, 26);
    check(afterFirst.red() < 40 && afterFirst.green() > 200 && afterFirst.blue() > 200, "after its first FX: inverted (cyan)");
    check(engine.setModelLayer(engine.indexOfId(screen), picture, false, &error, 0), "taken at its source");
    const QColor source = at(32, 26);
    check(source.red() > 200 && source.blue() < 40, "its source, before its FX");
    engine.setModelLayer(engine.indexOfId(screen), picture, false, &error, 1);
    // The plane is about 40 px wide here: its edge, then bent toward the camera (its sides come forward, larger)
    int edge = 0;
    for (int x = 32; x < 64 && at(x, 32).lightness() > 0; ++x) edge = x;
    engine.setParameter(screen, "model/bend", 120.0);
    int bentEdge = 0;
    for (int x = 32; x < 64 && at(x, 32).lightness() > 0; ++x) bentEdge = x;
    check(bentEdge != edge, "a bend curves the plane");
    engine.setParameter(screen, "model/bend", 0.0);
    // White everywhere pushes it toward the camera: it looks larger
    int before = 0;
    for (int x = 32; x < 64 && at(x, 32).lightness() > 0; ++x) before = x;
    check(engine.setModelDisplace(engine.indexOfId(screen), bump, &error), "a layer's luminance displaces the plane");
    engine.setParameter(screen, "model/displace/amount", 1.0);
    int pushed = 0;
    for (int x = 32; x < 64 && at(x, 32).lightness() > 0; ++x) pushed = x;
    check(pushed > before, "pushed toward the camera, it looks larger");
    int coarse = 0, fine = 0;
    {
        Engine::Lock lk(&engine.mutex());
        coarse = engine.layer(engine.indexOfId(screen))->model->triangleCount();
    }
    engine.setParameter(screen, "model/detail", 8);
    engine.renderFrame();
    {
        Engine::Lock lk(&engine.mutex());
        fine = engine.layer(engine.indexOfId(screen))->model->triangleCount();
    }
    check(fine > coarse * 4, "Mesh Detail cuts the primitive finer");
    // A sheet when asked (NYXMAP_UI_CAPTURE_DIR): a bent screen, a dome, a plane pushed by a luminance, lit
    if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) {
        const QString checker = dir.path() + "/Checker.fs", bumps = dir.path() + "/Bumps.fs";
        QFile c(checker), b(bumps);
        if (c.open(QIODevice::WriteOnly) && b.open(QIODevice::WriteOnly)) {
            c.write("/*{\"ISFVSN\":\"2\"}*/\nvoid main() { vec2 u = isf_FragNormCoord * 8.0; float k = mod(floor(u.x) + floor(u.y), 2.0);"
                    " gl_FragColor = vec4(mix(vec3(0.1, 0.2, 0.8), vec3(1.0, 0.8, 0.2), k) * (0.4 + 0.6 * isf_FragNormCoord.y), 1.0); }\n");
            b.write("/*{\"ISFVSN\":\"2\"}*/\nvoid main() { vec2 u = isf_FragNormCoord - 0.5; float r = length(u);"
                    " gl_FragColor = vec4(vec3(0.5 + 0.5 * cos(r * 40.0) * exp(-r * 4.0)), 1.0); }\n");
            c.close();
            b.close();
        }
        engine.setLayerIsf(engine.indexOfId(picture), checker, &error);
        engine.setLayerIsf(engine.indexOfId(bump), bumps, &error);
        engine.setCompositionSize({320, 240});
        engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {320, 240});
        QImage sheet(960, 240, QImage::Format_RGB32);
        QPainter painter(&sheet);
        auto shot = [&](int k, const QString &title) {
            engine.renderFrame();
            painter.drawImage(320 * k, 0, engine.grabOutput());
            painter.setPen(Qt::white);
            painter.drawText(320 * k + 6, 16, title);
        };
        engine.setModelDisplace(engine.indexOfId(screen), 0, &error);
        engine.setParameter(stage, "camera/yaw", 25.0);
        engine.setParameter(stage, "camera/pitch", 10.0);
        engine.setParameter(screen, "model/bend", 140.0);
        engine.setParameter(screen, "model/scale/x", 1.8);
        shot(0, "Plane · picture · bend 140° · unlit");
        engine.setLayerModel(engine.indexOfId(screen), Model3D::primitivePath("Dome"), &error);
        engine.setParameter(screen, "model/bend", 0.0);
        engine.setParameter(screen, "model/scale/x", 1.0);
        engine.setParameter(screen, "model/unlit", false);
        engine.setParameter(stage, "camera/pitch", 35.0);
        shot(1, "Dome · picture · lit");
        engine.setLayerModel(engine.indexOfId(screen), Model3D::primitivePath("Plane"), &error);
        engine.setModelPicture(engine.indexOfId(screen), 0, &error);
        engine.setModelDisplace(engine.indexOfId(screen), bump, &error);
        engine.setParameter(screen, "model/rotation/x", -60.0);
        engine.setParameter(screen, "model/displace/amount", 0.25);
        engine.setParameter(screen, "model/displace/mid", 0.5);
        const int key = engine.addLayer("Key", engine.indexOfId(screen) + 1);
        engine.setLayerLight(key);
        engine.setStructure(tree::intoGroup(engine.structure(), {engine.layerId(key)}, stage));
        shot(2, "Plane · displaced by a luminance · detail 4");
        painter.end();
        sheet.save(captures + "/surfaces.png");
        engine.setModelLayer(engine.indexOfId(screen), picture, false, &error, 1);
        engine.setCompositionSize({64, 64});
    }
    const QString surfaces = dir.path() + "/surfaces.nyxmap";
    check(engine.saveProject(surfaces, {}, &error), "save the surfaces");
    engine.newProject();
    check(engine.loadProject(surfaces, &ui, &error), "open the surfaces");
    {
        Engine::Lock lk(&engine.mutex());
        const Layer *p = engine.layer(engine.indexOfId(screen));
        check(p && p->modelPicture == picture && p->modelDisplace == bump, "its picture and displacement layers come back");
        check(p && p->modelPictureStage == 1 && p->modelDisplaceStage == -1, "and the stages they are taken at");
    }
    // An environment (a layer's picture, as an HDRI would be): a scene without lights lit by it alone, shown behind if asked
    {
        const QString redSky = dir.path() + "/RedSky.fs";
        QFile f(redSky);
        check(f.open(QIODevice::WriteOnly), "write the environment shader");
        f.write("/*{\"ISFVSN\":\"2\"}*/\nvoid main() { gl_FragColor = vec4(1.0, 0.15, 0.1, 1.0); }\n");
        f.close();
        const int ei = engine.addLayer("Sky");
        const quint64 sky = engine.layerId(ei);
        check(engine.setLayerIsf(ei, redSky, &error), "an environment layer");
        engine.setParameter(sky, "enable", false);
        const int s3 = engine.addScene();
        const quint64 lit = engine.layerId(s3);
        const int ball = engine.addLayer("Ball", s3 + 1);
        check(engine.setLayerModel(ball, Model3D::primitivePath("Sphere"), &error), "a sphere");
        engine.setStructure(tree::intoGroup(engine.structure(), {engine.layerId(ball)}, lit));
        engine.setParameter(lit, "scene/ambient_intensity", 0.0);
        for (int i = 0; i < engine.layerCount(); ++i) { // the other scenes out of the way
            Engine::Lock lk(&engine.mutex());
            if (Layer *o = engine.layer(i); o && o->scene3d && o->id != lit) o->enabled = false;
        }
        engine.setParameter(lit, "scene/studio", 0.0); // no studio's light either
        const QColor unlitBall = at(32, 32);
        check(unlitBall.lightness() < 10, "no light, no environment: dark");
        check(engine.setSceneEnvironment(engine.indexOfId(lit), sky, &error), "the layer surrounds the scene");
        check(!engine.setSceneEnvironment(engine.indexOfId(lit), lit, &error), "the scene itself is refused (a loop)");
        const QColor envBall = at(32, 32);
        check(envBall.red() > 60 && envBall.red() > 3 * envBall.blue(), "the environment lights the sphere (red)");
        check(at(1, 1).lightness() == 0, "not shown behind unless asked");
        engine.setParameter(lit, "scene/environment/show", true);
        const QColor behind = at(1, 1);
        check(behind.red() > 150 && behind.blue() < 80, "shown behind the models");
        engine.setParameter(lit, "scene/environment/intensity", 0.0);
        check(at(32, 32).lightness() < 10, "its intensity at 0: dark again");
        engine.setParameter(lit, "scene/environment/intensity", 1.0);
        if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) {
            // A sky (blue above, a warm horizon, a sun, a dark ground), a smooth metal ball, a rough one, a matte one
            const QString skyShader = dir.path() + "/Sky.fs";
            QFile f(skyShader);
            if (f.open(QIODevice::WriteOnly)) {
                f.write("/*{\"ISFVSN\":\"2\"}*/\nvoid main() { vec2 u = isf_FragNormCoord; float y = u.y * 2.0 - 1.0;"
                        " vec3 sky = mix(vec3(1.0, 0.75, 0.45), vec3(0.15, 0.35, 0.8), smoothstep(0.0, 0.6, y));"
                        " vec3 ground = mix(vec3(0.25, 0.2, 0.15), vec3(0.05), smoothstep(0.0, -0.5, y));"
                        " vec3 c = y > 0.0 ? sky : ground;"
                        " float sun = smoothstep(0.03, 0.0, distance(u, vec2(0.62, 0.68)));"
                        " gl_FragColor = vec4(c + sun * 4.0, 1.0); }\n");
                f.close();
            }
            engine.setLayerIsf(engine.indexOfId(sky), skyShader, &error);
            engine.setCompositionSize({480, 270});
            engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {480, 270});
            engine.setParameter(lit, "camera/distance", 4.0);
            engine.setParameter(lit, "camera/pitch", 8.0);
            const quint64 smooth = engine.layerId(ball);
            engine.setParameter(smooth, "model/position/x", -1.1);
            engine.setParameter(smooth, "model/material/metal", 1.0);
            engine.setParameter(smooth, "model/material/rough", 0.08);
            engine.setParameter(smooth, "model/material/color/r", 0.95);
            engine.setParameter(smooth, "model/material/color/g", 0.95);
            engine.setParameter(smooth, "model/material/color/b", 0.95);
            for (const auto &[x, metal, rough] : {std::tuple{0.0, 1.0, 0.45}, std::tuple{1.1, 0.0, 0.5}}) {
                const int k = engine.addLayer("Ball", engine.indexOfId(lit) + 1);
                engine.setLayerModel(k, Model3D::primitivePath("Sphere"), &error);
                const quint64 id = engine.layerId(k);
                engine.setStructure(tree::intoGroup(engine.structure(), {id}, lit));
                engine.setParameter(id, "model/position/x", x);
                engine.setParameter(id, "model/material/metal", metal);
                engine.setParameter(id, "model/material/rough", rough);
                engine.setParameter(id, "model/material/color/r", metal > 0 ? 1.0 : 0.8);
                engine.setParameter(id, "model/material/color/g", metal > 0 ? 0.75 : 0.25);
                engine.setParameter(id, "model/material/color/b", metal > 0 ? 0.35 : 0.2);
            }
            engine.renderFrame();
            engine.renderFrame();
            engine.grabOutput().save(captures + "/environment.png");
            engine.setCompositionSize({64, 64});
        }
        // An HDR file (Radiance .hdr, a red of 8.0): its values above 1 kept, read linear; brighter than its clipped picture
        {
            const QString hdrPath = dir.path() + "/red8.hdr";
            QFile f(hdrPath);
            check(f.open(QIODevice::WriteOnly), "write an HDR file");
            f.write("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n");
            for (int k = 0; k < 8; ++k) f.write(QByteArray::fromHex("80101084")); // 8.0, 1.0, 1.0 (RGBE)
            f.close();
            std::vector<float> px;
            QSize size;
            check(readHdrPixels(hdrPath, &px, &size) && size == QSize(4, 2) && px.size() == 32 && px[0] > 7.5 && px[0] < 8.5,
                  "an HDR file is read as floats, not clipped (8.0 stays 8.0)");
            // The same as an EXR in half floats (its usual), when FFmpeg can write one here
            const QString exrPath = dir.path() + "/red8h.exr";
            if (QProcess::execute("ffmpeg", {"-hide_banner", "-loglevel", "error", "-y", "-i", hdrPath, "-c:v", "exr", "-format", "half", exrPath}) == 0) {
                std::vector<float> ex;
                QSize exSize;
                check(readHdrPixels(exrPath, &ex, &exSize) && ex.size() == 32 && ex[0] > 7.5 && ex[0] < 8.5,
                      "a half-float EXR is read unclipped too");
            }
            const int hi = engine.addLayer("HDRI");
            const quint64 hdri = engine.layerId(hi);
            check(engine.setLayerFile(hi, hdrPath, &error), "an image layer loads the HDR file");
            engine.setParameter(hdri, "enable", false);
            engine.setParameter(lit, "scene/environment/show", false);
            check(engine.setSceneEnvironment(engine.indexOfId(lit), hdri, &error, -1), "the HDR layer, clipped");
            const QColor clipped = at(32, 32);
            check(engine.setSceneEnvironment(engine.indexOfId(lit), hdri, &error, Engine::kStageHdrFile), "the HDR file as read");
            const QColor full = at(32, 32);
            check(full.lightness() > clipped.lightness() + 10, "the unclipped HDR lights more than its clipped picture");
            engine.setSceneEnvironment(engine.indexOfId(lit), sky, &error);
        }
        const QString envProject = dir.path() + "/environment.nyxmap";
        check(engine.saveProject(envProject, {}, &error), "save the environment");
        engine.newProject();
        check(engine.loadProject(envProject, &ui, &error), "open the environment");
        {
            Engine::Lock lk(&engine.mutex());
            const Layer *l = engine.layer(engine.indexOfId(lit));
            check(l && l->sceneEnvironment == sky && l->sceneEnvironmentStage == -1, "the scene's environment comes back");
        }
    }

    // A scene of its own, seen from above: a floor, a cube over it, a spot right over them
    {
        engine.newProject();
        engine.setCompositionSize({128, 128});
        engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {128, 128});
        const int si2 = engine.addScene();
        const quint64 stage = engine.layerId(si2);
        const auto solid = [&](const QString &prim, double y, double size, double tilt) {
            const int at = engine.addLayer(prim, engine.indexOfId(stage) + 1);
            const quint64 id = engine.layerId(at);
            QString err;
            check(engine.setLayerModel(at, Model3D::primitivePath(prim), &err), "a primitive for the shadows");
            engine.setParameter(id, "model/position/y", y);
            engine.setParameter(id, "model/scale/all", size);
            engine.setParameter(id, "model/rotation/x", tilt);
            engine.setParameter(id, "model/material/rough", 1.0);
            return id;
        };
        const quint64 floorId = solid("Plane", 0, 10, -90);
        const quint64 caster = solid("Cube", 1.0, 0.25, 0);
        engine.setParameter(caster, "model/position/x", 0.6);
        const int lampAt = engine.addLayer("Spot", engine.indexOfId(stage) + 1);
        const quint64 spot = engine.layerId(lampAt);
        check(engine.setLayerLight(lampAt), "a light for the shadows");
        engine.setStructure(tree::intoGroup(engine.structure(), {floorId, caster, spot}, stage));
        engine.setParameter(spot, "light/type", QStringLiteral("spot"));
        engine.setParameter(spot, "light/position/x", 0.0);
        engine.setParameter(spot, "light/position/y", 2.0);
        engine.setParameter(spot, "light/position/z", 0.0);
        engine.setParameter(spot, "light/pitch", -89.0);
        engine.setParameter(spot, "light/angle", 50.0);
        engine.setParameter(spot, "light/intensity", 6.0);
        engine.setParameter(spot, "light/softness", 0.0);
        engine.setParameter(stage, "scene/studio", 0.0);
        engine.setParameter(stage, "scene/ambient_intensity", 0.0);
        engine.setParameter(stage, "camera/target/x", 0.0);
        engine.setParameter(stage, "camera/target/y", 0.0);
        engine.setParameter(stage, "camera/target/z", 0.0);
        engine.setParameter(stage, "camera/yaw", 0.0);
        engine.setParameter(stage, "camera/pitch", 89.0);
        engine.setParameter(stage, "camera/distance", 3.0);
        engine.setParameter(stage, "camera/fov", 60.0);
        engine.setParameter(caster, "enable", false);
        // Without a graphics card, what costs the most goes (shadows, contact shadows, reflections): the render stays usable
        const bool lite = Scene3D::lite(engine.glRenderer());
        const auto levels = [&](int x) {
            engine.renderFrame();
            return engine.grabOutput().pixelColor(x, 64).lightness();
        };
        // Falloff: at 0 the light holds to the edge; at 1 it fades from the middle
        engine.setParameter(spot, "light/falloff", 0.0);
        const int middleSharp = levels(64), asideSharp = levels(90);
        engine.setParameter(spot, "light/falloff", 1.0);
        const int middleSoft = levels(64), asideSoft = levels(90);
        check(middleSharp > 40, "the spot lights the floor under it");
        check(std::abs(middleSoft - middleSharp) < 16 && asideSoft < asideSharp - 10, "its falloff fades towards its edge, not its middle");
        // Shadows: the cube's on the floor, only when asked; a shadow takes nothing away beside it
        engine.setParameter(spot, "light/falloff", 0.2);
        const int aside = levels(80);
        engine.setParameter(caster, "enable", true);
        engine.setParameter(spot, "light/shadow", false);
        const int without = levels(112);
        engine.setParameter(spot, "light/shadow", true);
        const int with = levels(112);
        check(lite || with < without - 10, "the cube's shadow on the floor");
        check(std::abs(levels(80) - aside) < 12, "beside it, the floor as it was");
        // Its shadow maps are only drawn when what they show moves
        const auto renders = [&] {
            Engine::Lock lk(&engine.mutex());
            const Layer *l = engine.layer(engine.indexOfId(stage));
            return l && l->sceneRenderer ? l->sceneRenderer->shadowRenders() : -1;
        };
        engine.renderFrame();
        const int drawn = renders();
        engine.renderFrame();
        engine.renderFrame();
        check(lite || (drawn > 0 && renders() == drawn), "nothing moved: the shadow maps are not drawn again");
        engine.setParameter(caster, "model/position/z", 0.01);
        engine.renderFrame();
        check(lite || renders() == drawn + 1, "a model moved: they are");
        engine.setParameter(caster, "model/position/z", 0.0);
        // A point light's shadows fall on all sides of it
        engine.setParameter(spot, "light/type", QStringLiteral("point"));
        engine.setParameter(spot, "light/shadow", false);
        const int pointWithout = levels(112);
        engine.setParameter(spot, "light/shadow", true);
        const int pointWith = levels(112);
        check(lite || pointWith < pointWithout - 10, "a point light's shadow on the floor");
        engine.setParameter(spot, "light/position/y", 0.5);
        engine.setParameter(spot, "light/position/x", 0.6);
        engine.setParameter(caster, "model/position/y", 1.5);
        engine.setParameter(spot, "light/shadow", false);
        const int under = levels(64);
        engine.setParameter(spot, "light/shadow", true);
        check(std::abs(levels(64) - under) < 12, "and light goes on where nothing stops it");
        engine.setParameter(spot, "light/type", QStringLiteral("spot"));
        engine.setParameter(spot, "light/position/x", 0.0);
        engine.setParameter(spot, "light/position/y", 2.0);
        engine.setParameter(caster, "model/position/y", 1.0);
        // Where surfaces meet: darker with the contact shadows; away from them, as it was
        engine.setParameter(spot, "light/intensity", 0.0);
        engine.setParameter(stage, "scene/studio", 1.0);
        engine.setParameter(caster, "model/position/x", 0.0);
        engine.setParameter(caster, "model/position/y", 0.144);
        engine.setParameter(stage, "scene/ao", 0.0);
        const int openNear = levels(73), openFar = levels(120);
        engine.setParameter(stage, "scene/ao", 1.0);
        check(lite || levels(73) < openNear - 4, "contact shadows where the cube meets the floor");
        check(std::abs(levels(120) - openFar) < 6, "not away from it");
        // A smooth floor shows the cube over it, from the side
        engine.setParameter(stage, "camera/pitch", 25.0);
        engine.setParameter(floorId, "model/material/rough", 0.03);
        engine.setParameter(floorId, "model/material/metal", 1.0);
        engine.setParameter(caster, "model/material/color/r", 1.0);
        engine.setParameter(caster, "model/material/color/g", 0.1);
        engine.setParameter(caster, "model/material/color/b", 0.1);
        const auto twice = [&] {
            engine.renderFrame();
            engine.renderFrame();
            return engine.grabOutput();
        };
        engine.setParameter(stage, "scene/reflections", 0.0);
        const QImage dull = twice();
        engine.setParameter(stage, "scene/reflections", 1.0);
        const QImage mirror = twice();
        int redder = 0;
        for (int y = 0; y < mirror.height(); ++y)
            for (int x = 0; x < mirror.width(); ++x)
                redder += QColor(mirror.pixel(x, y)).red() > QColor(dull.pixel(x, y)).red() + 12;
        check(lite || redder > 40, "the floor reflects the red cube");
        engine.setParameter(floorId, "model/material/metal", 0.0);
        engine.setParameter(floorId, "model/material/rough", 1.0);
        engine.setParameter(stage, "scene/studio", 0.0);
        // The quality, the composition's for all its scenes: what is off is off, and it costs no map
        engine.setParameter(spot, "light/intensity", 6.0);
        engine.setParameter(stage, "scene/studio", 0.0);
        engine.setParameter(caster, "model/position/x", 0.6);
        engine.setParameter(caster, "model/position/y", 1.0);
        for (const char *level : {"low", "high", "medium"}) {
            engine.setParameter(0, "quality3d/shadows", QString::fromLatin1(level));
            check(lite || levels(112) < without - 10, "shadows at each level of quality");
        }
        engine.setParameter(0, "quality3d/shadows", QStringLiteral("off"));
        const int shadowsOff = levels(112);
        check(std::abs(shadowsOff - without) < 12, "shadows off: none");
        if (std::abs(shadowsOff - without) >= 12) std::printf("  off %d, without %d, aside %d\n", shadowsOff, without, levels(100));
        engine.setParameter(0, "quality3d/shadows", QStringLiteral("medium"));
        for (const char *level : {"off", "2x", "8x", "4x"}) {
            engine.setParameter(0, "quality3d/antialiasing", QString::fromLatin1(level));
            check(levels(64) > 0, "every antialiasing draws the scene");
        }
        for (const char *path : {"quality3d/contact", "quality3d/reflections"})
            for (const char *level : {"off", "low", "high", "medium"}) {
                engine.setParameter(0, QString::fromLatin1(path), QString::fromLatin1(level));
                check(levels(64) > 0, "every level of quality draws the scene");
            }
        engine.setParameter(0, "quality3d/shadows", QStringLiteral("high"));
        check(engine.quality3d().shadows == 3, "the composition says its quality");
        engine.setParameter(0, "quality3d/shadows", QStringLiteral("medium"));
        engine.setParameter(spot, "light/intensity", 0.0);
        engine.setParameter(caster, "model/position/x", 0.0);
        // The studio's light: something to reflect, without an environment
        engine.setParameter(spot, "light/intensity", 0.0);
        const int unlitFloor = levels(64);
        engine.setParameter(stage, "scene/studio", 1.0);
        check(levels(64) > unlitFloor, "the studio lights the models");
        engine.newProject();
    }

    // Mesh Detail: more when asked, and when a bend or a displacement need points to move
    {
        engine.newProject();
        const quint64 stage = engine.layerId(engine.addScene());
        const int at = engine.addLayer("Plane", engine.indexOfId(stage) + 1);
        const quint64 plane = engine.layerId(at);
        QString err;
        check(engine.setLayerModel(at, Model3D::primitivePath("Plane"), &err), "a plane to cut");
        engine.setStructure(tree::intoGroup(engine.structure(), {plane}, stage));
        const auto triangles = [&] {
            engine.renderFrame();
            Engine::Lock lk(&engine.mutex());
            const Layer *l = engine.layer(engine.indexOfId(plane));
            return l && l->model ? l->model->triangleCount() : -1;
        };
        check(triangles() == 2, "as it is: the least");
        engine.setParameter(plane, "model/detail", 3);
        check(triangles() == 2 * 8 * 8, "detail 3: as cut as it asks");
        engine.setParameter(plane, "model/detail", 1);
        check(triangles() == 2, "back to the least");
        engine.setParameter(plane, "model/bend", 90.0);
        check(triangles() == 2 * 64 * 64, "bent: as many as a curve needs");
        engine.setParameter(plane, "model/bend", 0.0);
        check(triangles() == 2, "straight again: the least");
        engine.newProject();
    }

    // Shading, the count, and what is not computed
    {
        engine.newProject();
        engine.setCompositionSize({128, 128});
        engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {128, 128});
        const quint64 stage = engine.layerId(engine.addScene());
        const int ballAt = engine.addLayer("Ball", engine.indexOfId(stage) + 1);
        const quint64 ball = engine.layerId(ballAt);
        QString err;
        check(engine.setLayerModel(ballAt, Model3D::primitivePath("Sphere"), &err), "a sphere to shade");
        const int boxAt = engine.addLayer("Box", engine.indexOfId(stage) + 1);
        const quint64 box = engine.layerId(boxAt);
        check(engine.setLayerModel(boxAt, Model3D::primitivePath("Cube"), &err), "a cube beside it");
        const int lampAt = engine.addLayer("Lamp", engine.indexOfId(stage) + 1);
        const quint64 lamp = engine.layerId(lampAt);
        check(engine.setLayerLight(lampAt), "a light for them");
        engine.setStructure(tree::intoGroup(engine.structure(), {ball, box, lamp}, stage));
        engine.setParameter(box, "model/position/x", -1.2);
        engine.setParameter(ball, "model/position/x", 0.5);
        engine.setParameter(box, "model/scale/all", 0.5);
        engine.setParameter(lamp, "light/position/x", 1.0);
        engine.setParameter(lamp, "light/position/y", 1.0);
        engine.setParameter(lamp, "light/position/z", 3.0);
        const auto picture = [&] {
            engine.renderFrame();
            engine.renderFrame();
            return engine.grabOutput();
        };
        const auto different = [](const QImage &x, const QImage &y) {
            int count = 0;
            for (int j = 0; j < x.height(); ++j)
                for (int i = 0; i < x.width(); ++i) count += std::abs(QColor(x.pixel(i, j)).lightness() - QColor(y.pixel(i, j)).lightness()) > 6;
            return count;
        };
        // Its shading: facets on a sphere, rounded edges on a cube; back as it was
        const QImage made = picture();
        engine.setParameter(ball, "model/shading", QStringLiteral("flat"));
        check(different(made, picture()) > 30, "flat: the sphere's facets show");
        engine.setParameter(ball, "model/shading", QStringLiteral("as made"));
        engine.setParameter(box, "model/shading", QStringLiteral("smooth"));
        check(different(made, picture()) > 30, "smooth: the cube's edges are rounded");
        engine.setParameter(box, "model/shading", QStringLiteral("as made"));
        check(different(made, picture()) < 6, "as made: as it was");
        // The count: what it holds, shown
        const int sceneRow = engine.indexOfId(stage);
        const auto triangleOf = [&](quint64 id) {
            Engine::Lock lk(&engine.mutex());
            return engine.layer(engine.indexOfId(id))->model->triangleCount();
        };
        check(engine.sceneTriangles(sceneRow) == triangleOf(ball) + triangleOf(box), "the scene counts its triangles");
        engine.setParameter(box, "enable", false);
        check(engine.sceneTriangles(sceneRow) == triangleOf(ball), "only what is shown");
        engine.setParameter(box, "enable", true);
        // Nothing moves: its picture is kept
        const auto framesOf = [&] {
            Engine::Lock lk(&engine.mutex());
            return engine.layer(engine.indexOfId(stage))->sceneRenderer->framesDrawn();
        };
        for (int k = 0; k < 6; ++k) engine.renderFrame();
        const int settled = framesOf();
        for (int k = 0; k < 6; ++k) engine.renderFrame();
        check(framesOf() == settled, "nothing moved: not drawn again");
        check(engine.sceneTrianglesComputed(sceneRow) == 0, "and nothing computed");
        const QImage kept = engine.grabOutput();
        engine.setParameter(ball, "model/position/y", 0.1);
        engine.renderFrame();
        check(framesOf() == settled + 1, "something moved: drawn");
        const int seen = engine.sceneTrianglesComputed(sceneRow);
        check(seen > 0, "and computed");
        check(different(kept, engine.grabOutput()) > 0, "and it shows");
        // Out of the camera's view: not computed; the rest as it was
        engine.setParameter(ball, "model/position/x", 200.0);
        engine.renderFrame();
        check(engine.sceneTrianglesComputed(sceneRow) < seen / 2, "what no one sees is not computed");
        engine.newProject();
    }

    // A Merge 3D: not a render, a transform for what it holds; one can hold another
    {
        engine.newProject();
        engine.setCompositionSize({128, 128});
        engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), {128, 128});
        const quint64 stage = engine.layerId(engine.addScene());
        const int cubeAt = engine.addLayer("Cube", engine.indexOfId(stage) + 1);
        const quint64 cubeId = engine.layerId(cubeAt);
        QString err;
        check(engine.setLayerModel(cubeAt, Model3D::primitivePath("Cube"), &err), "a cube in a merge");
        const int lampAt = engine.addLayer("Lamp", engine.indexOfId(stage) + 1);
        const quint64 lampId = engine.layerId(lampAt);
        check(engine.setLayerLight(lampAt), "a light in a merge");
        const quint64 outer = engine.layerId(engine.addMerge3d("Outer", engine.indexOfId(stage) + 1));
        const quint64 inner = engine.layerId(engine.addMerge3d("Inner", engine.indexOfId(stage) + 1));
        LayerTree nested = tree::intoGroup(engine.structure(), {outer}, stage);
        check(tree::valid3d(nested) == false, "not yet: a Merge 3D's objects are still outside");
        nested = tree::intoGroup(engine.structure(), {lampId, outer}, stage);
        nested = tree::intoGroup(nested, {inner}, outer);
        nested = tree::intoGroup(nested, {cubeId}, inner);
        check(tree::valid3d(nested), "a scene holds a Merge 3D, which holds another, which holds a model");
        engine.setStructure(nested);
        const LayerTree shown = engine.structure();
        check(shown[size_t(tree::indexOf(shown, inner))].role == TreeNode::Merge3d, "the structure knows a Merge 3D");
        check(!tree::valid3d(tree::ungrouped(shown, stage)), "a Merge 3D out of its scene is refused");
        const int plain = engine.addLayer("Plain");
        check(!tree::valid3d(tree::intoGroup(engine.structure(), {engine.layerId(plain)}, inner)), "a 2D layer in a Merge 3D is refused");
        engine.removeLayer(engine.indexOfId(engine.layerId(plain)));
        engine.setParameter(cubeId, "model/scale/all", 0.2);
        engine.setParameter(lampId, "light/type", QStringLiteral("directional"));
        engine.setParameter(lampId, "light/intensity", 3.0);
        engine.setParameter(lampId, "light/yaw", 0.0);
        engine.setParameter(lampId, "light/pitch", -89.0);
        engine.setParameter(lampId, "light/shadow", false);
        engine.setParameter(stage, "camera/target/x", 0.0);
        engine.setParameter(stage, "camera/target/y", 0.0);
        engine.setParameter(stage, "camera/target/z", 0.0);
        engine.setParameter(stage, "camera/yaw", 0.0);
        engine.setParameter(stage, "camera/pitch", 0.0);
        engine.setParameter(stage, "camera/distance", 4.0);
        engine.setParameter(stage, "camera/fov", 40.0);
        engine.setParameter(stage, "scene/studio", 1.0);
        const auto lit = [&](int x, int y) {
            engine.renderFrame();
            return engine.grabOutput().pixelColor(x, y).lightness() > 12;
        };
        check(lit(64, 64), "what it holds is drawn by the scene");
        // Each moves what it holds, and they add up: 4 away at 40° of view, 0.5 is 22 pixels of 128
        engine.setParameter(outer, "merge/position/x", 0.5);
        check(!lit(64, 64) && lit(64 + 22, 64), "a Merge 3D moves what it holds");
        engine.setParameter(inner, "merge/position/x", 0.5);
        check(!lit(64 + 22, 64) && lit(64 + 44, 64), "one inside another: both");
        // Its transform turns and scales around itself: the outer one turned a quarter, the inner offset goes up
        engine.setParameter(outer, "merge/position/x", 0.0);
        engine.setParameter(outer, "merge/rotation/z", 90.0);
        check(!lit(64 + 22, 64) && lit(64, 64 - 22), "it turns what it holds, with what is inside it");
        engine.setParameter(outer, "merge/rotation/z", 0.0);
        engine.setParameter(outer, "merge/scale/all", 2.0);
        check(!lit(64 + 22, 64) && lit(64 + 44, 64), "it scales where what it holds is");
        engine.setParameter(outer, "merge/scale/all", 1.0);
        // Hidden: all it holds
        engine.setParameter(inner, "merge/position/x", 0.0);
        check(lit(64, 64), "back in the middle");
        engine.setParameter(outer, "enable", false);
        check(!lit(64, 64), "a hidden Merge 3D hides what it holds");
        engine.setParameter(outer, "enable", true);
        // It moves a light too: in front of the cube it lights it, carried behind it no more
        {
            const quint64 holder = engine.layerId(engine.addMerge3d("Lamp holder", engine.indexOfId(stage) + 1));
            LayerTree moved = tree::intoGroup(engine.structure(), {holder}, stage);
            moved = tree::intoGroup(moved, {lampId}, holder);
            engine.setStructure(moved);
            engine.setParameter(stage, "scene/studio", 0.0);
            engine.setParameter(stage, "scene/ambient_intensity", 0.0);
            engine.setParameter(lampId, "light/type", QStringLiteral("point"));
            engine.setParameter(lampId, "light/position/x", 0.0);
            engine.setParameter(lampId, "light/position/y", 0.0);
            engine.setParameter(lampId, "light/position/z", 2.0);
            check(lit(64, 64), "a light in a Merge 3D lights the cube");
            engine.setParameter(holder, "merge/position/z", -4.0);
            check(!lit(64, 64), "carried behind it, no more");
            engine.setParameter(holder, "merge/position/z", 0.0);
            engine.setParameter(holder, "merge/rotation/y", 180.0);
            check(!lit(64, 64), "turned around, no more either");
        }
        // Saved, and read back
        const QJsonObject json = engine.layerJson(engine.indexOfId(inner));
        check(json.value("merge3d").toBool(), "saved as a Merge 3D");
        const QString file = dir.filePath("merge.nyxmap");
        engine.setParameter(outer, "merge/position/x", 0.5);
        engine.setParameter(0, "quality3d/reflections", QStringLiteral("low")); // saved with the project
        check(engine.saveProject(file, {}, &err), "a project with Merge 3Ds is saved");
        engine.newProject();
        check(engine.loadProject(file, nullptr, &err), "and opened");
        bool found = false;
        for (int i = 0; i < engine.layerCount(); ++i) {
            Engine::Lock lk(&engine.mutex());
            const Layer *l = engine.layer(i);
            if (l && l->merge3d && l->name == "Outer") found = l->isGroup && std::abs(l->modelPlacement.position[0] - 0.5f) < 1e-4f;
        }
        check(found, "its transform comes back");
        check(engine.quality3d().reflections == 1, "the 3D quality comes back with the project");
        engine.newProject();
        check(engine.quality3d().reflections == 2, "a new project: the default quality");
        check(tree::valid3d(engine.structure()), "the nesting too");
        engine.newProject();
    }

    // What a frame costs: the parts of a file drawn at one go, what has not changed not drawn again
    {
        engine.renderFrame();
        auto *g = QOpenGLContext::currentContext()->extraFunctions();
        const auto read = [&](GLuint tex, int w, int h) {
            GLuint fbo = 0;
            g->glGenFramebuffers(1, &fbo);
            g->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            g->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            QImage shot(w, h, QImage::Format_RGBA8888_Premultiplied);
            g->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, shot.bits());
            g->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            g->glDeleteFramebuffers(1, &fbo);
            return shot;
        };
        // Two parts, one material: as read two meshes, both drawn
        const QString parts = dir.path() + "/parts.obj";
        {
            QFile f(parts);
            check(f.open(QIODevice::WriteOnly), "write the parts");
            QByteArray text;
            for (int part = 0; part < 2; ++part) {
                const float x = part ? 1.0f : -2.0f;
                text += "o part" + QByteArray::number(part) + "\n";
                for (int c = 0; c < 8; ++c)
                    text += "v " + QByteArray::number(x + (c & 1)) + " " + QByteArray::number((c >> 1) & 1) + " " + QByteArray::number((c >> 2) & 1) + "\n";
                static const int faces[12][3] = {{0, 2, 1}, {1, 2, 3}, {4, 5, 6}, {5, 7, 6}, {0, 1, 4}, {1, 5, 4}, {2, 6, 3}, {3, 6, 7}, {0, 4, 2}, {2, 4, 6}, {1, 3, 5}, {3, 7, 5}};
                for (const auto &t : faces)
                    text += "f " + QByteArray::number(part * 8 + 1 + t[0]) + " " + QByteArray::number(part * 8 + 1 + t[1]) + " " + QByteArray::number(part * 8 + 1 + t[2]) + "\n";
            }
            f.write(text);
        }
        std::vector<Scene3D::Item> items(1);
        items[0].model = std::make_shared<Model3D>();
        QString err;
        check(items[0].model->load(parts, &err) && items[0].model->meshCount() == 2 && items[0].model->triangleCount() == 24,
              "a file's parts are counted as read");
        items[0].placement.fit = true;
        Scene3DQuality plain;
        plain.antialiasing = plain.contact = plain.reflections = 0; // what a picture shows does not depend on the frames before
        std::vector<Scene3D::Lamp> lamps(2);
        lamps[0].light.type = lamps[1].light.type = 2;
        lamps[0].light.position = {{-2, 3, 3}};
        lamps[1].light.position = {{2, 3, 3}};
        lamps[0].light.angle = lamps[1].light.angle = 60;
        lamps[0].light.yaw = 145;
        lamps[1].light.yaw = -145;
        SceneSettings camera;
        camera.yaw = camera.pitch = 0;
        const int w = 128, h = 64;
        Scene3D scene;
        QImage shot = read(scene.draw(camera, items, lamps, w, h, 0, false, plain), w, h);
        const auto covered = [&](int from, int to) {
            int count = 0;
            for (int y = 0; y < h; ++y)
                for (int x = from; x < to; ++x) count += shot.pixelColor(x, y).alpha() > 0;
            return count;
        };
        check(covered(0, w / 2 - 6) > 50 && covered(w / 2 + 6, w) > 50, "both its parts are drawn");
        check(covered(w / 2 - 2, w / 2 + 2) == 0, "and not what is between them");
        // Its shadow maps: each one only when what it shows has changed (without a graphics card, no shadows)
        const bool lite = Scene3D::lite(QString::fromLatin1(reinterpret_cast<const char *>(g->glGetString(GL_RENDERER))));
        const auto maps = [&](int drawn) { return scene.shadowMapsDrawn() == (lite ? 0 : drawn); };
        check(maps(2), "its two lights' maps drawn");
        camera.yaw = 1;
        scene.draw(camera, items, lamps, w, h, 0, false, plain);
        check(maps(2), "the camera moving draws none");
        lamps[0].light.position[0] = -2.5f;
        scene.draw(camera, items, lamps, w, h, 0, false, plain);
        check(maps(3), "a light moving draws its own, not the other's");
        items[0].placement.position[0] = 0.01f;
        scene.draw(camera, items, lamps, w, h, 0, false, plain);
        check(maps(5), "a model moving draws them both");
        // A light away while a model moved: its map, out of date, is drawn again when it comes back
        const float power = lamps[1].light.intensity;
        lamps[1].light.intensity = 0;
        scene.draw(camera, items, lamps, w, h, 0, false, plain);
        items[0].placement.rotation[1] = 30; // turned where it is: its light's map is made from the same place
        scene.draw(camera, items, lamps, w, h, 0, false, plain);
        lamps[1].light.intensity = power;
        const QImage back = read(scene.draw(camera, items, lamps, w, h, 0, false, plain), w, h);
        Scene3D fresh;
        const QImage whole = read(fresh.draw(camera, items, lamps, w, h, 0, false, plain), w, h);
        check(back == whole, "a light coming back shows the shadows of now");
        fresh.releaseGl();
        // An HDR file around it does not change: its picture is kept. A layer's picture can change at any frame
        const auto environment = [&] {
            GLuint tex = 0;
            const float texels[4] = {1, 1, 1, 1};
            g->glGenTextures(1, &tex);
            g->glBindTexture(GL_TEXTURE_2D, tex);
            g->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, texels);
            g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            g->glBindTexture(GL_TEXTURE_2D, 0);
            return tex;
        };
        const GLuint hdr = environment(), other = environment();
        Scene3D lit;
        const auto frames = [&](GLuint tex, bool linear) {
            const int before = lit.framesDrawn();
            for (int k = 0; k < 8; ++k) lit.draw(camera, items, lamps, w, h, tex, linear, plain);
            return lit.framesDrawn() - before;
        };
        check(frames(hdr, true) == 3, "under an HDR file, a still scene's picture is kept");
        check(frames(other, true) == 3, "another file: drawn again, then kept");
        check(frames(hdr, false) == 8, "under a layer's picture, drawn at each frame");
        lit.releaseGl();
        // A view's light drawing: one colour and its edges, whatever the lights; what has not changed is not drawn again
        {
            Scene3D plain;
            std::vector<Scene3D::Item> one(1);
            one[0].model = std::make_shared<Model3D>();
            check(one[0].model->load(Model3D::primitivePath("Cube"), &err), "a cube to draw plainly");
            one[0].placement.fit = true;
            one[0].placement.rotation = {{20, 30, 0}};
            one[0].placement.color = {{1, 0, 0}};
            camera.yaw = 0;
            camera.background = {{0, 0, 0, 0}};
            const int size = 96;
            shot = read(plain.drawView(camera, one, size, size), size, size);
            QHash<QRgb, int> inks;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) ++inks[shot.pixel(x, y)];
            check(inks.size() == 3, "only three colours: around it, its faces, its edges");
            int edge = 0, face = 0;
            for (auto ink = inks.cbegin(); ink != inks.cend(); ++ink) {
                if (qAlpha(ink.key()) == 0) continue;
                (ink.value() > 400 ? face : edge) = ink.value();
            }
            check(face > 400 && edge > 40 && edge < 600, "its edges drawn over its faces, a cube's only");
            check(plain.framesDrawn() == 1 && plain.trianglesComputed() == 12, "a cube's twelve triangles");
            plain.drawView(camera, one, size, size);
            check(plain.framesDrawn() == 1 && plain.trianglesComputed() == 0, "nothing moved: kept");
            camera.yaw = 10;
            plain.drawView(camera, one, size, size);
            check(plain.framesDrawn() == 2, "the camera moving: drawn again");
            // Unlit: its colour, and a layer's picture on it, as they are, whatever the lights
            const auto inkAt = [&](QRgb wanted, int by) {
                const QColor seen = shot.pixelColor(size / 2, size / 2), want(wanted);
                return std::abs(seen.red() - want.red()) <= by && std::abs(seen.green() - want.green()) <= by && std::abs(seen.blue() - want.blue()) <= by;
            };
            shot = read(plain.drawView(camera, one, size, size, Scene3D::ViewLook::Unlit), size, size);
            check(inkAt(qRgb(255, 0, 0), 2), "unlit: its colour as it is");
            const int unlitFrames = plain.framesDrawn();
            plain.drawView(camera, one, size, size, Scene3D::ViewLook::Unlit);
            check(plain.framesDrawn() == unlitFrames, "unlit, nothing moved: kept");
            GLuint picture = 0;
            const unsigned char blue[4] = {0, 0, 255, 255};
            g->glGenTextures(1, &picture);
            g->glBindTexture(GL_TEXTURE_2D, picture);
            g->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, blue);
            g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            g->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            g->glBindTexture(GL_TEXTURE_2D, 0);
            one[0].picture = picture;
            shot = read(plain.drawView(camera, one, size, size, Scene3D::ViewLook::Unlit), size, size);
            check(inkAt(qRgb(0, 0, 255), 2), "unlit: a layer's picture on it");
            const int withPicture = plain.framesDrawn();
            plain.drawView(camera, one, size, size, Scene3D::ViewLook::Unlit);
            check(plain.framesDrawn() == withPicture + 1, "a picture can change at any frame: drawn again");
            shot = read(plain.drawView(camera, one, size, size), size, size);
            check(!inkAt(qRgb(0, 0, 255), 40), "simple: no picture");
            g->glDeleteTextures(1, &picture);
            plain.releaseGl();
            one[0].model->releaseGl();
        }
        g->glDeleteTextures(1, &hdr);
        g->glDeleteTextures(1, &other);
        scene.releaseGl();
        items[0].model->releaseGl();
    }

    // A light's Look At and path
    {
        LightSettings l;
        l.position = {{1, 2, 3}};
        const auto near = [](const QVector3D &a, const QVector3D &b) { return (a - b).length() < 1e-3f; };
        check(near(l.located(), QVector3D(1, 2, 3)) && l.pathOutline().empty(), "no path: a light stays at its position");
        l.pathShape = 1; // a circle, 4 across, around its position
        check(near(l.pathPoint(0), QVector3D(3, 2, 3)) && near(l.pathPoint(0.25), QVector3D(1, 2, 5)) && near(l.pathPoint(1.5), QVector3D(-1, 2, 3)),
              "around a circle, in the X Z plane");
        l.pathRotation = {{90, 0, 0}};
        check(near(l.pathPoint(0.25), QVector3D(1, 0, 3)), "its path tilted in 3D");
        l.pathRotation = {{0, 0, 0}};
        l.pathShape = 2; // a rectangle: 6 by 2
        l.pathWidth = 6;
        l.pathDepth = 2;
        check(near(l.pathPoint(0), QVector3D(-2, 2, 2)) && near(l.pathPoint(6.0 / 16), QVector3D(4, 2, 2)) && near(l.pathPoint(8.0 / 16), QVector3D(4, 2, 4)) &&
                  near(l.pathPoint(14.0 / 16), QVector3D(-2, 2, 4)) && near(l.pathPoint(3.0 / 16), QVector3D(1, 2, 2)),
              "along a rectangle's sides, at an even pace");
        check(l.pathOutline().size() == 5 && near(l.pathOutline().front(), l.pathOutline().back()), "its outline is closed");

        const quint64 stage = engine.layerId(engine.addScene("Light paths"));
        const int boxAt = engine.addLayer("Box", engine.indexOfId(stage) + 1);
        const quint64 boxId = engine.layerId(boxAt);
        QString err;
        check(engine.setLayerModel(boxAt, Model3D::primitivePath("Cube"), &err), "a cube to look at");
        const int lampAt = engine.addLayer("Lamp", engine.indexOfId(stage) + 1);
        const quint64 lampId = engine.layerId(lampAt);
        check(engine.setLayerLight(lampAt), "a light that looks at it");
        const quint64 mergeId = engine.layerId(engine.addMerge3d("Carried", engine.indexOfId(stage) + 1));
        LayerTree tree3d = tree::intoGroup(engine.structure(), {lampId, mergeId}, stage);
        tree3d = tree::intoGroup(tree3d, {boxId}, mergeId);
        engine.setStructure(tree3d);
        engine.setParameter(lampId, "light/type", QStringLiteral("spot"));
        engine.setParameter(lampId, "light/position/x", 0.0);
        engine.setParameter(lampId, "light/position/y", 4.0);
        engine.setParameter(lampId, "light/position/z", 0.0);
        engine.setParameter(lampId, "light/lookat", true);
        const auto placed = [&](QVector3D *target = nullptr) {
            Engine::Lock lk(&engine.mutex());
            return engine.placedLight(*engine.layer(engine.indexOfId(lampId)), QMatrix4x4(), target);
        };
        const auto aimOf = [](const LightSettings &light) { return Camera3D::aim(light.yaw, light.pitch); };
        check(near(aimOf(placed()), QVector3D(0, -1, 0)), "it looks at its point: straight down at the middle");
        engine.setParameter(lampId, "light/lookpoint/x", 4.0);
        engine.setParameter(lampId, "light/lookpoint/y", 4.0);
        check(near(aimOf(placed()), QVector3D(1, 0, 0)), "and along where the point goes");
        check(!engine.setLightLookAt(engine.indexOfId(lampId), lampId), "not at itself");
        check(engine.setLightLookAt(engine.indexOfId(lampId), boxId), "at an object of its scene");
        engine.setParameter(mergeId, "merge/position/x", 4.0); // the merge carries the cube: the light follows it
        engine.setParameter(mergeId, "merge/position/y", 4.0);
        QVector3D target;
        const LightSettings following = placed(&target);
        check(near(target, QVector3D(4, 4, 0)) && near(aimOf(following), QVector3D(1, 0, 0)), "it follows the object through its Merge 3D");
        const QJsonObject kept = engine.layerJson(engine.indexOfId(lampId));
        check(engine.setLightLookAt(engine.indexOfId(lampId), 0), "back to its point");
        engine.replaceLayerJson(engine.indexOfId(lampId), kept);
        {
            Engine::Lock lk(&engine.mutex());
            check(engine.layer(engine.indexOfId(lampId))->lightLookAt == boxId, "what it looks at is kept with the layer");
        }
        engine.setParameter(lampId, "light/lookat", false);
        engine.setParameter(lampId, "light/path/shape", QStringLiteral("circle"));
        const QVector3D start(2, 4, 0);
        const auto at = [&](const LightSettings &light) { return QVector3D(light.position[0], light.position[1], light.position[2]); };
        check(near(at(placed()), start), "it goes on its path from its position");
        engine.setParameter(lampId, "light/path/position", 0.5);
        check(near(at(placed()), QVector3D(-2, 4, 0)), "the path's position places it");
        engine.setParameter(lampId, "light/path/position", 0.0);
        engine.setParameter(lampId, "light/path/speed", 1.0);
        engine.renderFrame();
        const QVector3D first = at(placed());
        QThread::msleep(40);
        engine.renderFrame();
        check(!near(at(placed()), first), "its speed takes it along");
        engine.setParameter(lampId, "light/path/speed", 0.0);
        engine.removeLayer(engine.indexOfId(mergeId));
        engine.removeLayer(engine.indexOfId(lampId));
        engine.removeLayer(engine.indexOfId(stage));
    }

    return failures;
}
