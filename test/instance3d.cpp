#include "Engine.h"
#include "Model3D.h"
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
    engine.newProject();
    return failures;
}
