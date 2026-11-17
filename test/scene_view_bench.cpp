// What the interface's 3D view costs: a project's scene drawn as a view (Simple or Unlit) at the size of a widget and read
// back each frame, with the render thread running. Usage: nyxmap_scene_view_bench project.nyxmap [seconds] [WxH]
#include "Engine.h"
#include "FrameTiming.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QThread>
#include <algorithm>
#include <cstdio>
#include <vector>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc < 2) return 2;
    const double seconds = argc > 2 ? QString(argv[2]).toDouble() : 5;
    QSize size(1200, 800);
    if (argc > 3) size = QSize(QString(argv[3]).section('x', 0, 0).toInt(), QString(argv[3]).section('x', 1, 1).toInt());
    Engine e;
    QString err;
    if (!e.initialize(&err)) { std::printf("no renderer: %s\n", qPrintable(err)); return 1; }
    QString warn;
    if (!e.loadProject(QString::fromLocal8Bit(argv[1]), nullptr, &warn)) { std::printf("cannot load\n"); return 1; }
    quint64 scene = 0;
    for (int i = 0; i < e.layerCount() && !scene; ++i) {
        Engine::Lock lk(&e.mutex());
        if (e.layer(i)->scene3d) scene = e.layer(i)->id;
    }
    std::printf("renderer %s, scene layer %llu, view %dx%d\n", qPrintable(e.gpuRenderer()), (unsigned long long)scene, size.width(), size.height());
    e.start();
    auto run = [&](const char *what, bool view, Scene3D::ViewLook look) {
        if (view) {
            SceneSettings cam;
            e.setPreviewSource(0, scene);
            e.setPreviewScene(0, true, cam, size, look);
        } else {
            e.setPreviewScene(0, false, SceneSettings(), size, look);
        }
        QThread::msleep(1500); // settle
        const auto before = e.frameTiming();
        QElapsedTimer t;
        t.start();
        QImage last;
        int changes = 0;
        while (t.elapsed() < seconds * 1000) {
            QImage img = e.previewImage(0);
            if (!img.isNull() && img.cacheKey() != last.cacheKey()) { ++changes; last = img; }
            QThread::msleep(2);
        }
        const auto after = e.frameTiming();
        const double s = t.elapsed() / 1000.0;
        std::printf("%-22s engine %.1f fps (frame compute mean %.1f ms), view pictures %.1f /s\n", what,
                    double(after.frames - before.frames) / s, after.compute.mean(), changes / s);
        std::printf("   %s\n", qPrintable(e.renderLoopSummary()));
    };
    // The camera of the view moved at every mouse event (about 120 a second), as in a drag
    auto orbit = [&](const char *what, Scene3D::ViewLook look) {
        SceneSettings cam;
        e.setPreviewSource(0, scene);
        QThread::msleep(500);
        const auto before = e.frameTiming();
        QElapsedTimer t;
        t.start();
        QImage last;
        int changes = 0;
        float yaw = 0;
        while (t.elapsed() < seconds * 1000) {
            yaw += 1.0f;
            cam.yaw = yaw;
            e.setPreviewScene(0, true, cam, size, look);
            const QImage img = e.previewImage(0);
            if (!img.isNull() && img.cacheKey() != last.cacheKey()) { ++changes; last = img; }
            QThread::msleep(8);
        }
        const auto after = e.frameTiming();
        const double s = t.elapsed() / 1000.0;
        std::printf("%-22s engine %.1f fps (frame compute mean %.1f ms), view pictures %.1f /s\n", what,
                    double(after.frames - before.frames) / s, after.compute.mean(), changes / s);
    };
    // What the interface feels: how long it waits for the engine's lock (every mouse event, every panel reading a value)
    auto uiWait = [&](const char *what) {
        std::vector<double> waits;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < seconds * 1000) {
            QElapsedTimer w;
            w.start();
            { Engine::Lock lk(&e.mutex()); }
            waits.push_back(w.nsecsElapsed() / 1e6);
            QThread::usleep(700);
        }
        std::sort(waits.begin(), waits.end());
        double sum = 0;
        for (double v : waits) sum += v;
        std::printf("%-22s UI waits for the lock: mean %.2f ms, p95 %.2f, p99 %.2f, max %.2f (%zu times)\n", what, sum / double(waits.size()),
                    waits[waits.size() * 95 / 100], waits[waits.size() * 99 / 100], waits.back(), waits.size());
    };
    uiWait("PBR, no view");
    run("no 3D view", false, Scene3D::ViewLook::Simple);
    orbit("orbit, Simple", Scene3D::ViewLook::Simple);
    orbit("orbit, Unlit", Scene3D::ViewLook::Unlit);
    run("3D view Simple", true, Scene3D::ViewLook::Simple);
    run("3D view Unlit", true, Scene3D::ViewLook::Unlit);
    std::printf("render lock held per frame (lifetime): %s\n",
                QJsonDocument(e.frameTimingJson().value("render_lock_hold_lifetime").toObject()).toJson(QJsonDocument::Compact).constData());
    e.stop();
    return 0;
}
