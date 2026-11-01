#include "Engine.h"
#include "ImageFile.h"
#include "Model3D.h"
#include <QColor>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
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
    engine.setParameter(screen, "model/detail", 4);
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

    return failures;
}
