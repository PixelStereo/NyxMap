#include "Engine.h"
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

    // In a scene: a light and the camera along a Path
    engine.newProject();
    const int si = engine.addScene();
    const quint64 scene = engine.layerId(si);
    const int ls = engine.addLayer("Path", si + 1);
    check(engine.setLayerPath(ls), "a layer becomes a Path");
    const quint64 shape = engine.layerId(ls);
    const int ll = engine.addLayer("Lamp", ls + 1);
    engine.setLayerLight(ll);
    const quint64 light = engine.layerId(ll);
    const int le = engine.addMerge3d("Empty 1", ll + 1);
    const quint64 empty = engine.layerId(le);
    engine.setStructure(tree::intoGroup(engine.structure(), {shape, light, empty}, scene));
    check(tree::valid3d(engine.structure()), "a Path belongs in a 3D scene");
    const auto at = [&](quint64 id) { return engine.indexOfId(id); };
    engine.setParameter(shape, "path/position/y", 3.0);
    check(engine.setLightPath(at(light), shape), "a light travels along a Path of its scene");
    check(!engine.setLightPath(at(light), empty), "not along what is not a Path");
    engine.setParameter(light, "light/path/position", 0.25);
    {
        Engine::Lock lk(&engine.mutex());
        const LightSettings placed = engine.placedLight(*engine.layer(at(light)), QMatrix4x4());
        check(near(QVector3D(placed.position[0], placed.position[1], placed.position[2]), {0, 3, 2}),
              "the light is where the shape's path puts it, the shape moved");
    }
    check(engine.setCameraPath(at(scene), shape) && engine.setCameraLookAt(at(scene), empty), "the camera along it, at the Empty");
    engine.setParameter(empty, "merge/position/x", 1.0);
    {
        Engine::Lock lk(&engine.mutex());
        const SceneSettings cam = engine.placedCamera(*engine.layer(at(scene)));
        const Camera3D c = Camera3D::of(cam, 1);
        check(near(c.eye, {2, 3, 0}) && near(QVector3D(cam.target[0], cam.target[1], cam.target[2]), {1, 0, 0}),
              "the camera is on its path, looking at the Empty");
    }
    engine.setParameter(scene, "camera/path/speed", 0.5);
    {
        Engine::Lock lk(&engine.mutex());
        engine.stepAnimations(0.5);
    }
    {
        Engine::Lock lk(&engine.mutex());
        const Camera3D c = Camera3D::of(engine.placedCamera(*engine.layer(at(scene))), 1);
        check(near(c.eye, {0, 3, 2}), "its speed moves it along (a quarter turn in half a second at half a turn per second)");
    }

    // Saved and read back
    QTemporaryDir dir;
    QString err;
    engine.setParameter(shape, "path/kind", QStringLiteral("spiral"));
    engine.setParameter(shape, "path/points", 64);
    const QString file = dir.filePath("shapes.nyxmap");
    check(engine.saveProject(file, {}, &err), "a project with a Path is saved");
    engine.newProject();
    check(engine.loadProject(file, nullptr, &err), "and opened");
    {
        Engine::Lock lk(&engine.mutex());
        const Layer *s = engine.layer(engine.indexOfId(shape));
        const Layer *l = engine.layer(engine.indexOfId(light));
        const Layer *sc = engine.layer(engine.indexOfId(scene));
        check(s && s->type == SourceType::Path && s->path3d.kind == Path3D::Spiral && s->path3d.points == 64 &&
                  std::abs(s->modelPlacement.position[1] - 3) < 1e-4f, "the shape comes back");
        check(l && l->lightPath == shape && sc && sc->cameraPath == shape && sc->cameraLookAt == empty,
              "the paths and what the camera looks at come back");
    }
    engine.newProject();
    return failures;
}
