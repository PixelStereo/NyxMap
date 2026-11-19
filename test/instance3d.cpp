#include "Engine.h"
#include "fx3d_test.h"
#include "Model3D.h"
#include "Path3D.h"
#include <algorithm>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

// Instances: models that show another model's meshes and material, with their own place and 3D FX
int runInstance3dTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL instance3d: %s\n", why); }
    };
    engine.newProject();
    const int si = engine.addScene();
    const quint64 scene = engine.layerId(si);
    const auto at = [&](quint64 id) { return engine.indexOfId(id); };
    const auto add = [&](const char *name) { return engine.layerId(engine.addLayer(name, at(scene) + 1)); };
    QString err;
    const quint64 cube = add("Cube"), copy = add("Cube Instance"), again = add("Instance of an instance"), lamp = add("Lamp");
    check(engine.setLayerModel(at(cube), Model3D::primitivePath(QStringLiteral("Sphere")), &err), "the original");
    engine.setLayerLight(at(lamp));
    check(!engine.setLayerInstance(at(copy), lamp), "an instance of what is not a model is refused");
    check(engine.setLayerInstance(at(copy), cube), "an instance of a model");
    check(engine.setLayerInstance(at(again), copy), "an instance of an instance");
    engine.setStructure(tree::intoGroup(engine.structure(), {cube, copy, again, lamp}, scene));
    {
        Engine::Lock lk(&engine.mutex());
        const Layer *o = engine.layer(at(cube)), *c = engine.layer(at(copy)), *a = engine.layer(at(again));
        check(c->instanceOf == cube && a->instanceOf == cube, "an instance of an instance shows the original");
        check(!c->model && engine.modelOf(*c) == o->model && engine.originalOf(*a) == o,
              "an instance holds no model: it shows its original's");
    }
    const int triangles = [&] {
        Engine::Lock lk(&engine.mutex());
        return engine.layer(at(cube))->model->triangleCount();
    }();
    check(engine.sceneTriangles(at(scene)) == 3 * triangles, "the scene counts every instance's triangles");

    // Its own place
    engine.setParameter(copy, "model/position/x", 2.0);
    const auto placeOf = [&](quint64 id) {
        Engine::Lock lk(&engine.mutex());
        return engine.placeOf3dObject(*engine.layer(at(id)));
    };
    check((placeOf(copy) - placeOf(cube) - QVector3D(2, 0, 0)).length() < 1e-3f, "an instance has its own place");

    // Drawn with its original's meshes; removing an instance leaves the original whole
    engine.renderFrame();
    engine.renderFrame();
    const int drawn = engine.sceneTrianglesComputed(at(scene)); // in all its passes (shadows too)
    check(drawn >= 3 * triangles, "the original and its instances are drawn");
    engine.removeLayer(at(again));
    engine.renderFrame();
    engine.renderFrame();
    const int afterRemoval = engine.sceneTrianglesComputed(at(scene));
    check(afterRemoval * 3 == drawn * 2, "removing an instance keeps its original's meshes: two spheres of three still drawn");

    // Own material: a parameter; saved and read back
    check(engine.setParameter(copy, "model/instance/own_material", true), "Own Material");
    engine.setParameter(copy, "model/material/color/r", 0.1);
    QTemporaryDir dir;
    const QString file = dir.filePath("instances.nyxmap");
    check(engine.saveProject(file, {}, &err), "a project with instances is saved");
    engine.newProject();
    check(engine.loadProject(file, nullptr, &err), "and opened");
    {
        Engine::Lock lk(&engine.mutex());
        const Layer *c = engine.layer(engine.indexOfId(copy));
        const Layer *o = engine.layer(engine.indexOfId(cube));
        check(c && o && c->type == SourceType::Model && c->instanceOf == cube && !c->model && c->instanceOwnMaterial &&
                  std::abs(c->modelPlacement.color[0] - 0.1f) < 1e-4f && std::abs(c->modelPlacement.position[0] - 2) < 1e-4f &&
                  engine.modelOf(*c) == o->model,
              "the instance, its original, its place and its own material come back");
    }

    // The Duplicate 3D FX: a copy on every point of a Path
    engine.newProject();
    const int s2 = engine.addScene();
    const quint64 stage = engine.layerId(s2);
    const auto add2 = [&](const char *name) { return engine.layerId(engine.addLayer(name, at(stage) + 1)); };
    const quint64 grid = add2("Grid"), box = add2("Box"), twin = add2("Twin");
    engine.setLayerPath(at(grid));
    engine.setParameter(grid, "path/kind", QStringLiteral("grid"));
    engine.setParameter(grid, "path/columns", 3);
    engine.setParameter(grid, "path/rows", 2);
    engine.setParameter(grid, "path/position/y", 1.0);
    engine.setLayerModel(at(box), Model3D::primitivePath(QStringLiteral("Cube")), &err);
    engine.setLayerInstance(at(twin), box);
    engine.setStructure(tree::intoGroup(engine.structure(), {grid, box, twin}, stage));
    const int cubeTriangles = [&] {
        Engine::Lock lk(&engine.mutex());
        return engine.layer(at(box))->model->triangleCount();
    }();
    check(engine.sceneTriangles(at(stage)) == 2 * cubeTriangles, "without the FX: one of each");
    check(engine.setDuplicatePath(at(box), grid), "Duplicate on a grid");
    engine.setParameter(box, fxPath(engine, box, Fx3d::Duplicate, "enabled"), false); // a FX is added on: this one waits
    check(engine.sceneTriangles(at(stage)) == 2 * cubeTriangles, "the FX off: nothing copied yet");
    engine.setParameter(box, fxPath(engine, box, Fx3d::Duplicate, "enabled"), true);
    check(engine.sceneTriangles(at(stage)) == 7 * cubeTriangles, "the FX on: a copy on each of the 6 points, and the instance");
    {
        Engine::Lock lk(&engine.mutex());
        const auto frames = engine.placementFrames(*engine.layer(at(box)), QMatrix4x4(), true);
        bool onPoints = frames.size() == 6;
        Path3D g;
        g.kind = Path3D::Grid, g.columns = 3, g.rows = 2;
        const auto points = g.pointsLocal();
        for (size_t k = 0; onPoints && k < frames.size(); ++k)
            onPoints = (frames[k].map(QVector3D()) - (points[k] + QVector3D(0, 1, 0))).length() < 1e-3f;
        check(onPoints, "each copy is on its point, where the Path is");
    }
    engine.setParameter(box, fxPath(engine, box, Fx3d::Duplicate, "random_scale"), 0.5);
    engine.setParameter(box, fxPath(engine, box, Fx3d::Duplicate, "random_rotation"), 90.0);
    const auto sizes = [&] {
        Engine::Lock lk(&engine.mutex());
        std::vector<float> out;
        for (const QMatrix4x4 &f : engine.placementFrames(*engine.layer(at(box)), QMatrix4x4(), true)) out.push_back(f.column(1).toVector3D().length());
        return out;
    };
    const auto first = sizes();
    check(first == sizes() && std::any_of(first.begin(), first.end(), [&](float v) { return std::abs(v - first[0]) > 1e-3f; }),
          "variations differ from copy to copy, the same at every frame");
    engine.setParameter(box, fxPath(engine, box, Fx3d::Duplicate, "seed"), 7);
    check(sizes() != first, "another seed, other variations");
    check(engine.setDuplicatePath(at(twin), grid), "an Instance has its own Duplicate");
    check(engine.sceneTriangles(at(stage)) == 12 * cubeTriangles, "the instance copied too, with the same meshes");
    // Far enough for every copy to be in view: what the camera does not see is not drawn
    engine.setParameter(stage, "camera/distance", 30.0);
    engine.renderFrame();
    engine.renderFrame();
    check(engine.sceneTrianglesComputed(at(stage)) >= 12 * cubeTriangles, "every copy is drawn");
    const QString file2 = dir.filePath("duplicates.nyxmap");
    check(engine.saveProject(file2, {}, &err), "a project with Duplicate FX is saved");
    engine.newProject();
    check(engine.loadProject(file2, nullptr, &err), "and opened");
    {
        Engine::Lock lk(&engine.mutex());
        const Layer *b = engine.layer(engine.indexOfId(box));
        const Fx3dEntry *d = b ? b->fx3dFirst(Fx3d::Duplicate) : nullptr;
        check(d && d->s.layer == grid && d->s.seed == 7 && std::abs(d->s.randomScale - 0.5f) < 1e-4f, "the Duplicate FX comes back");
    }

    // The Spin 3D FX: a rotation that goes on by itself, around the object's own centre or a point
    engine.newProject();
    const int s3 = engine.addScene();
    const quint64 hall = engine.layerId(s3);
    const auto add3 = [&](const char *name) { return engine.layerId(engine.addLayer(name, at(hall) + 1)); };
    const quint64 ring = add3("Ring"), crate = add3("Crate"), clone = add3("Clone");
    engine.setLayerPath(at(ring));
    engine.setParameter(ring, "path/kind", QStringLiteral("grid"));
    engine.setParameter(ring, "path/columns", 3);
    engine.setParameter(ring, "path/rows", 2);
    check(engine.setLayerModel(at(crate), Model3D::primitivePath(QStringLiteral("Cube")), &err), "a model to spin");
    check(engine.setLayerInstance(at(clone), crate), "and an instance of it");
    engine.setStructure(tree::intoGroup(engine.structure(), {ring, crate, clone}, hall));
    engine.setParameter(crate, "model/position/x", 2.0);
    const auto frameOf = [&](quint64 id) {
        Engine::Lock lk(&engine.mutex());
        return engine.placementFrames(*engine.layer(at(id)), QMatrix4x4(), true);
    };
    const auto placeAt = [&](quint64 id) {
        Engine::Lock lk(&engine.mutex());
        return engine.placeOf3dObject(*engine.layer(at(id)));
    };
    const auto meshOf = [&](quint64 id) {
        Engine::Lock lk(&engine.mutex());
        return engine.modelOf(*engine.layer(at(id)));
    };
    const int crateTriangles = meshOf(crate)->triangleCount();
    check(engine.sceneTriangles(at(hall)) == 2 * crateTriangles, "a Spin scene: the model and its instance");
    check(frameOf(crate).size() == 1 && frameOf(crate)[0] == QMatrix4x4(), "no Spin: the frame is the parent's");
    const quint64 spin = engine.addFx3d(at(crate), Fx3d::Spin);
    check(spin != 0 && engine.addFx3d(at(clone), Fx3d::Spin) != 0, "a model and an instance can Spin");
    check(!engine.addFx3d(at(ring), Fx3d::Spin), "not what is not a model");
    const auto set = [&](quint64 layer, quint64 fx, const char *name, const QJsonValue &v) {
        engine.setParameter(layer, QStringLiteral("fx3d/%1/%2").arg(fx).arg(QLatin1String(name)), v);
    };
    set(crate, spin, "enabled", false);
    set(crate, spin, "angle", 90.0);
    check(frameOf(crate)[0] == QMatrix4x4(), "the FX off: the object stays as it was");
    set(crate, spin, "enabled", true);
    const QMatrix4x4 turned = frameOf(crate)[0];
    check(turned != QMatrix4x4(), "the FX on: the object is turned");
    check((placeAt(crate) - QVector3D(2, 0, 0)).length() < 1e-3f, "around its own centre: it stays where it is");
    check(turned == frameOf(crate)[0], "stable between frames while its speed is 0");
    set(crate, spin, "pivot", QStringLiteral("a point"));
    check((placeAt(crate) - QVector3D(0, 0, -2)).length() < 1e-3f, "around a point (the origin): it goes around it");
    set(crate, spin, "point/x", 2.0);
    check((placeAt(crate) - QVector3D(2, 0, 0)).length() < 1e-3f, "around a point on it: it stays");
    set(crate, spin, "pivot", QStringLiteral("its own centre"));
    // Its speed: a quarter turn per second, so half a second is an eighth of a turn
    set(crate, spin, "angle", 0.0);
    set(crate, spin, "speed", 0.25);
    const QMatrix4x4 before = frameOf(crate)[0];
    {
        Engine::Lock lk(&engine.mutex());
        engine.stepAnimations(4.0); // a whole turn: back where it was
    }
    check((frameOf(crate)[0].column(0) - before.column(0)).length() < 1e-3f, "a whole turn brings it back");
    {
        Engine::Lock lk(&engine.mutex());
        engine.stepAnimations(0.5);
    }
    const QMatrix4x4 eighth = frameOf(crate)[0];
    QMatrix4x4 expected;
    expected.translate(2, 0, 0);
    expected.rotate(45.f, 0, 1, 0);
    expected.translate(-2, 0, 0);
    check((eighth.column(0) - expected.column(0)).length() < 1e-3f && (eighth.column(3) - expected.column(3)).length() < 1e-3f,
          "its speed turns it: half a second, an eighth of a turn");
    set(crate, spin, "axis", QStringLiteral("z"));
    check(engine.parameterValue(crate, QStringLiteral("fx3d/%1/axis").arg(spin)).toString() == "z" &&
              (frameOf(crate)[0].column(0) - eighth.column(0)).length() > 0.1f,
          "another axis, another turn");
    set(crate, spin, "axis", QStringLiteral("y"));
    // It shares the mesh and builds nothing: the same meshes, the same triangles
    check(meshOf(clone) == meshOf(crate) && engine.sceneTriangles(at(hall)) == 2 * crateTriangles, "the instance keeps the original's mesh and the count");
    engine.renderFrame();
    engine.renderFrame();
    const int spinDrawn = engine.sceneTrianglesComputed(at(hall));
    check(spinDrawn >= 2 * crateTriangles, "a spinning object is drawn");
    // Duplicate then Spin: each copy turns on itself; Spin then Duplicate: copies stand on the points
    check(engine.setDuplicatePath(at(crate), ring), "Duplicate on a grid");
    check(frameOf(crate).size() == 6 && engine.sceneTriangles(at(hall)) == 7 * crateTriangles, "6 copies, and the instance");
    {
        Engine::Lock lk(&engine.mutex());
        Path3D g;
        g.kind = Path3D::Grid, g.columns = 3, g.rows = 2;
        const auto points = g.pointsLocal();
        const auto frames = engine.placementFrames(*engine.layer(at(crate)), QMatrix4x4(), true);
        bool onPoints = frames.size() == 6, turnedAll = true;
        for (size_t k = 0; onPoints && k < frames.size(); ++k) {
            // Duplicate comes after Spin in the list: each copy is on its point, the Spin applied before it
            onPoints = (frames[k].map(QVector3D()) - points[k]).length() < 1e-3f;
            turnedAll = turnedAll && frames[k].column(0).toVector3D().length() > 0.5f;
        }
        check(onPoints && turnedAll, "Spin then Duplicate: every copy on its point");
        // Spin after Duplicate: each copy turns on its own centre
        const quint64 second = engine.addFx3d(at(crate), Fx3d::Spin);
        set(crate, second, "angle", 90.0);
        const auto after = engine.placementFrames(*engine.layer(at(crate)), QMatrix4x4(), true);
        check(after.size() == 6, "Duplicate then Spin: still 6 copies");
        engine.removeFx3d(at(crate), second);
    }
    engine.renderFrame();
    engine.renderFrame();
    engine.setParameter(hall, "camera/distance", 30.0); // far enough for every copy to be in view
    engine.renderFrame();
    engine.renderFrame();
    check(engine.sceneTrianglesComputed(at(hall)) >= 7 * crateTriangles, "the copies are drawn, turning");
    // Saved and read back
    set(crate, spin, "speed", 0.5);
    set(crate, spin, "angle", 30.0);
    set(crate, spin, "pivot", QStringLiteral("a point"));
    set(crate, spin, "point/y", 1.5);
    const QString file3 = dir.filePath("spin.nyxmap");
    check(engine.saveProject(file3, {}, &err), "a project with a Spin FX is saved");
    engine.newProject();
    check(engine.loadProject(file3, nullptr, &err), "and opened");
    {
        Engine::Lock lk(&engine.mutex());
        const Layer *c = engine.layer(engine.indexOfId(crate));
        const Fx3dEntry *f = c ? c->fx3dFirst(Fx3d::Spin) : nullptr;
        check(f && f->id == spin && f->s.align == 1 && f->s.axis == 1 && std::abs(f->s.speed - 0.5f) < 1e-4f &&
                  std::abs(f->s.angle - 30) < 1e-3f && std::abs(f->s.move[1] - 1.5f) < 1e-4f,
              "the Spin FX comes back with its settings");
    }
    engine.newProject();
    return failures;
}
