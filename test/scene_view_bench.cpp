// What the interface's 3D view costs: a project's scene drawn as a view (Simple or Unlit) at the size of a widget and read
// back each frame, with the render thread running. Usage: nyxmap_scene_view_bench project.nyxmap [seconds] [WxH]
#include "Engine.h"
#include "FrameTiming.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QThread>
#include <algorithm>
#include <cmath>
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
    if (!qEnvironmentVariableIsSet("BENCH_MOVING_ONLY")) {   // manual mode, as the GL measurement: frames in a row, then one picture
        for (int k = 0; k < 30; ++k) e.renderFrame();
        e.grabOutput();
        QElapsedTimer t;
        t.start();
        const int n = 400;
        for (int k = 0; k < n; ++k) { e.renderFrame(); e.grabOutput(); }
        const QImage last = e.grabOutput();
        std::printf("MANUAL %d frames: %.2f ms each (picture %dx%d, centre %06x)\n", n, t.elapsed() / double(n), last.width(), last.height(),
                    last.pixel(last.width() / 2, last.height() / 2) & 0xffffff);
    }
    e.start();
    // How fast the engine itself goes, uncapped, with 10-bit (RGBA16F) and 8-bit pictures
    for (const int depth : qEnvironmentVariableIsSet("BENCH_MOVING_ONLY") ? std::vector<int>{} : std::vector<int>{10, 8}) {
        Engine::RenderSettings r;
        r.frameRate = Engine::RenderSettings::UncappedFrameRate;
        r.depth = depth;
        e.setRenderSettings(r);
        QThread::msleep(1500);
        const auto before = e.frameTiming();
        QThread::msleep(int(seconds * 1000));
        const auto after = e.frameTiming();
        std::printf("uncapped, %2d-bit        engine %.1f fps (frame compute mean %.1f ms)\n", depth,
                    double(after.frames - before.frames) / seconds, after.compute.mean());
    }
    Engine::RenderSettings normal;
    e.setRenderSettings(normal);
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
    // Something moves in the scene every frame: an object turns (what a show does), uncapped
    {
        Engine::RenderSettings r;
        r.frameRate = Engine::RenderSettings::UncappedFrameRate;
        e.setRenderSettings(r);
        quint64 mover = 0;
        for (int i = 0; i < e.layerCount() && !mover; ++i) {
            Engine::Lock lk(&e.mutex());
            if (e.layer(i)->name.startsWith(argc > 4 ? QString(argv[4]) : QStringLiteral("Polished Gold"))) mover = e.layer(i)->id;
        }
        QThread::msleep(800);
        const auto before = e.frameTiming();
        QElapsedTimer t;
        t.start();
        double angle = 0;
        while (t.elapsed() < seconds * 1000) {
            angle += 2;
            const bool set = e.setParameter(mover, "model/position/x", std::sin(angle / 50.0) * 2.0);
            if (angle == 2) std::printf("setParameter -> %d, reads %f\n", int(set), e.parameterValue(mover, "model/position/x").toDouble());
            QThread::usleep(500);
        }
        const auto after = e.frameTiming();
        const double s = t.elapsed() / 1000.0;
        std::printf("MOVING object %llu       engine %.1f fps (frame compute mean %.1f ms, p95 %.1f ms)\n", (unsigned long long)mover,
                    double(after.frames - before.frames) / s, after.compute.mean(), after.compute.percentile(0.95));
        e.setRenderSettings(Engine::RenderSettings());
    }
    {   // The camera of the scene turns (an orbit in a show, or in the 3D view), uncapped
        Engine::RenderSettings r;
        r.frameRate = Engine::RenderSettings::UncappedFrameRate;
        e.setRenderSettings(r);
        QThread::msleep(800);
        const auto before = e.frameTiming();
        QElapsedTimer t;
        t.start();
        double yaw = 0;
        const QImage first = e.grabOutput();
        while (t.elapsed() < seconds * 1000) {
            yaw += 0.5;
            e.setParameter(scene, "camera/yaw", yaw);
            QThread::usleep(500);
        }
        const auto after = e.frameTiming();
        const double sec = t.elapsed() / 1000.0;
        std::printf("   camera picture changed: %d (yaw reads %f)\n", int(e.grabOutput() != first), e.parameterValue(scene, "camera/yaw").toDouble());
        std::printf("MOVING camera            engine %.1f fps (frame compute mean %.1f ms, p95 %.1f ms)\n", double(after.frames - before.frames) / sec,
                    after.compute.mean(), after.compute.percentile(0.95));
        e.setRenderSettings(Engine::RenderSettings());
    }
    if (qEnvironmentVariableIsSet("BENCH_MOVING_ONLY")) { e.stop(); return 0; }
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
