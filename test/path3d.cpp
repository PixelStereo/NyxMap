#include "Engine.h"
#include "Model3D.h"
#include "Path3D.h"
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

// Paths (Path3D.h): points laid out by a rule; lights and the scene camera travel along them
int runPath3dTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL path3d: %s\n", why); }
    };
    const auto near = [](const QVector3D &a, const QVector3D &b) { return (a - b).length() < 1e-3f; };

    // Geometry
    Path3D circle;
    circle.points = 8;
    const auto ring = circle.pointsLocal();
    check(ring.size() == 8 && near(ring[0], {2, 0, 0}) && near(ring[2], {0, 0, 2}), "a circle's points go evenly around");
    check(near(circle.along(0.25), {0, 0, 2}) && near(circle.along(1.25), circle.along(0.25)), "a circle is a closed path");
    Path3D grid;
    grid.kind = Path3D::Grid;
    grid.columns = 3, grid.rows = 2;
    check(grid.pointCount() == 6 && grid.pointsLocal().size() == 6 && grid.linesLocal().size() == 5 && !grid.isPath(),
          "a grid: columns × rows points, its rows and columns as lines");
    Path3D rect;
    rect.kind = Path3D::Rectangle;
    check(near(rect.along(0), {-2, 0, -2}) && near(rect.along(0.5), {2, 0, 2}), "a rectangle is travelled at an even pace");
    Path3D line;
    line.kind = Path3D::Line;
    check(near(line.along(0), {-2, 0, 0}) && near(line.along(1), {2, 0, 0}) && near(line.along(1.5), line.along(0.5)),
          "an open path goes to its end and back");
    Path3D random, same;
    random.kind = same.kind = Path3D::Random;
    random.points = same.points = 50;
    random.height = same.height = 2;
    check(random.pointsLocal() == same.pointsLocal(), "random points depend on their seed only");
    same.seed = 2;
    check(random.pointsLocal() != same.pointsLocal(), "another seed, other points");
    bool inside = true;
    for (const QVector3D &q : random.pointsLocal()) inside = inside && std::abs(q.x()) <= 2 && std::abs(q.y()) <= 1 && std::abs(q.z()) <= 2;
    check(inside, "random points stay in their box");
    Path3D sphere;
    sphere.kind = Path3D::Sphere;
    sphere.points = 100;
    bool onSphere = true;
    for (const QVector3D &q : sphere.pointsLocal()) onSphere = onSphere && std::abs(q.length() - 2) < 1e-3f;
    check(sphere.pointsLocal().size() == 100 && onSphere, "a sphere's points lie on it");

    // In a scene: Follow Path 3D FX on a light, a camera, a model and a Merge 3D; cameras as objects
    engine.newProject();
    const int si = engine.addScene();
    const quint64 scene = engine.layerId(si);
    const auto add = [&](const char *name) { return engine.layerId(engine.addLayer(name, engine.indexOfId(scene) + 1)); };
    const quint64 path = add("Path");
    check(engine.setLayerPath(engine.indexOfId(path)), "a layer becomes a Path");
    const quint64 light = add("Lamp");
    engine.setLayerLight(engine.indexOfId(light));
    const quint64 camera = add("Cam A"), other = add("Cam B");
    check(engine.setLayerCamera(engine.indexOfId(camera)) && engine.setLayerCamera(engine.indexOfId(other)),
          "layers become cameras");
    const quint64 model = add("Box");
    QString err;
    check(engine.setLayerModel(engine.indexOfId(model), Model3D::primitivePath(QStringLiteral("Cube")), &err), "a primitive");
    const quint64 empty = engine.layerId(engine.addMerge3d("Empty 1", engine.indexOfId(scene) + 1));
    const quint64 held = add("Held");
    engine.setLayerCamera(engine.indexOfId(held));
    engine.setStructure(tree::intoGroup(engine.structure(), {path, light, camera, other, model, empty}, scene));
    engine.setStructure(tree::intoGroup(engine.structure(), {held}, empty));
    check(tree::valid3d(engine.structure()), "Paths, cameras and Empties belong in a 3D scene");
    const auto at = [&](quint64 id) { return engine.indexOfId(id); };
    engine.setParameter(path, "path/position/y", 3.0);

    // Follow Path is a 3D FX: nothing moves until it is on
    check(engine.setFollowPath(at(light), path), "a light can follow a Path of its scene");
    check(!engine.setFollowPath(at(light), empty), "not what is not a Path");
    engine.setParameter(light, "follow/position", 0.25);
    const auto placeOf = [&](quint64 id) {
        Engine::Lock lk(&engine.mutex());
        return engine.placeOf3dObject(*engine.layer(at(id)));
    };
    check(!near(placeOf(light), {0, 3, 2}), "the FX off: the light stays where it is");
    engine.setParameter(light, "follow/enabled", true);
    check(near(placeOf(light), {0, 3, 2}), "the FX on: the light is on the path, the path moved");

    // A model follows, its own position an offset; turned along its path
    engine.setFollowPath(at(model), path);
    engine.setParameter(model, "follow/enabled", true);
    engine.setParameter(model, "follow/position", 0.0);
    check(near(placeOf(model), {2, 3, 0}), "a model on its path");
    // A quarter turn round: it goes toward -X, so its own +X offset points to +Z
    engine.setParameter(model, "follow/orient", true);
    engine.setParameter(model, "follow/position", 0.25);
    engine.setParameter(model, "model/position/x", 1.0);
    check(near(placeOf(model), {0, 3, 3}), "turned along it, its offset turns too");

    // A Merge 3D (an Empty) follows: what it holds comes along
    engine.setFollowPath(at(empty), path);
    engine.setParameter(empty, "follow/enabled", true);
    engine.setParameter(empty, "follow/position", 0.5);
    engine.setParameter(held, "camera3d/position/x", 0.0);
    engine.setParameter(held, "camera3d/position/y", 1.0);
    engine.setParameter(held, "camera3d/position/z", 0.0);
    check(near(placeOf(empty), {-2, 3, 0}) && near(placeOf(held), {-2, 4, 0}), "an Empty on its path carries what it holds");

    // Cameras: several per scene, the scene seen through the one it chooses, looking at an object
    check(engine.setActiveCamera(at(scene), camera), "the scene is seen through a camera object");
    check(!engine.setActiveCamera(at(scene), model), "not through what is not a camera");
    check(engine.setCameraLookAt(at(camera), empty), "a camera looks at an Empty");
    check(engine.setCameraLookAt(at(other), model), "another looks at a model");
    engine.setFollowPath(at(camera), path);
    engine.setParameter(camera, "follow/enabled", true);
    engine.setParameter(camera, "follow/position", 0.75);
    {
        Engine::Lock lk(&engine.mutex());
        const SceneSettings view = engine.placedCamera(*engine.layer(at(scene)));
        const Camera3D c = Camera3D::of(view, 1);
        check(near(c.eye, {0, 3, -2}) && near(QVector3D(view.target[0], view.target[1], view.target[2]), {-2, 3, 0}),
              "the active camera is on its path, looking at the Empty");
    }
    engine.setParameter(camera, "follow/speed", 0.5);
    {
        Engine::Lock lk(&engine.mutex());
        engine.stepAnimations(0.5);
        const Camera3D c = Camera3D::of(engine.placedCamera(*engine.layer(at(scene))), 1);
        check(near(c.eye, {2, 3, 0}), "its speed moves it along (a quarter turn in half a second)");
    }
    engine.setActiveCamera(at(scene), other);
    {
        Engine::Lock lk(&engine.mutex());
        const Camera3D c = Camera3D::of(engine.placedCamera(*engine.layer(at(scene))), 1);
        check(near(c.eye, {3, 2, 4}), "switching cameras: the scene seen from the other one");
    }

    // Saved and read back
    QTemporaryDir dir;
    engine.setParameter(path, "path/kind", QStringLiteral("spiral"));
    engine.setParameter(path, "path/points", 64);
    const QString file = dir.filePath("paths.nyxmap");
    check(engine.saveProject(file, {}, &err), "a project with Paths and cameras is saved");
    engine.newProject();
    check(engine.loadProject(file, nullptr, &err), "and opened");
    {
        Engine::Lock lk(&engine.mutex());
        const Layer *p = engine.layer(engine.indexOfId(path));
        const Layer *c = engine.layer(engine.indexOfId(camera));
        const Layer *o = engine.layer(engine.indexOfId(other));
        const Layer *sc = engine.layer(engine.indexOfId(scene));
        const Layer *l = engine.layer(engine.indexOfId(light));
        check(p && p->type == SourceType::Path && p->path3d.kind == Path3D::Spiral && p->path3d.points == 64 &&
                  std::abs(p->modelPlacement.position[1] - 3) < 1e-4f, "the Path comes back");
        check(c && c->type == SourceType::Camera && c->followPath == path && c->followEnabled && c->lookAtObject == empty &&
                  o && o->lookAtObject == model && sc && sc->activeCamera == other,
              "the cameras, what they look at and the one the scene is seen through come back");
        check(l && l->followPath == path && l->followEnabled, "a Follow Path FX comes back");
    }
    engine.newProject();
    return failures;
}
