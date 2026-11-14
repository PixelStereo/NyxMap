#include "Engine.h"
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
    check(engine.sceneTriangles(at(stage)) == 2 * cubeTriangles, "the FX off: nothing copied yet");
    engine.setParameter(box, "duplicate/enabled", true);
    check(engine.sceneTriangles(at(stage)) == 7 * cubeTriangles, "the FX on: a copy on each of the 6 points, and the instance");
    {
        Engine::Lock lk(&engine.mutex());
        const auto frames = engine.duplicateFrames(*engine.layer(at(box)), QMatrix4x4());
        bool onPoints = frames.size() == 6;
        Path3D g;
        g.kind = Path3D::Grid, g.columns = 3, g.rows = 2;
        const auto points = g.pointsLocal();
        for (size_t k = 0; onPoints && k < frames.size(); ++k)
            onPoints = (frames[k].map(QVector3D()) - (points[k] + QVector3D(0, 1, 0))).length() < 1e-3f;
        check(onPoints, "each copy is on its point, where the Path is");
    }
    engine.setParameter(box, "duplicate/random_scale", 0.5);
    engine.setParameter(box, "duplicate/random_rotation", 90.0);
    const auto sizes = [&] {
        Engine::Lock lk(&engine.mutex());
        std::vector<float> out;
        for (const QMatrix4x4 &f : engine.duplicateFrames(*engine.layer(at(box)), QMatrix4x4())) out.push_back(f.column(1).toVector3D().length());
        return out;
    };
    const auto first = sizes();
    check(first == sizes() && std::any_of(first.begin(), first.end(), [&](float v) { return std::abs(v - first[0]) > 1e-3f; }),
          "variations differ from copy to copy, the same at every frame");
    engine.setParameter(box, "duplicate/seed", 7);
    check(sizes() != first, "another seed, other variations");
    check(engine.setDuplicatePath(at(twin), grid), "an Instance has its own Duplicate");
    engine.setParameter(twin, "duplicate/enabled", true);
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
        check(b && b->duplicateEnabled && b->duplicatePath == grid && b->duplicateSeed == 7 &&
                  std::abs(b->duplicateRandomScale - 0.5f) < 1e-4f, "the Duplicate FX comes back");
    }
    engine.newProject();
    return failures;
}
