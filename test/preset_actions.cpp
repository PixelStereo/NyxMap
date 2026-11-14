// Actions in a preset: a media's Rewind and Seek To, a countdown's Start, a timeline's Play — held as something to do,
// done when the preset is recalled, after its values. A whole category holds its values, not its actions.
#include "Engine.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QThread>
#include <QSurfaceFormat>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char *why)
{
    if (!ok) throw std::runtime_error(why);
}

int main(int argc, char **argv)
{
    QSurfaceFormat f;
    f.setVersion(3, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(f);
    QGuiApplication app(argc, argv);
    try {
        Engine e;
        QString err;
        require(e.initialize(&err), qPrintable(err));
        e.newProject();
        e.setFadesManual(true);
        const QString media = QStringLiteral(TEST_DIR) + "/media/av.mp4";
        const int vi = e.addLayer("Movie");
        const quint64 movie = e.layerId(vi);
        require(e.setLayerVideo(vi, media, &err), qPrintable(err));
        const int ci = e.addLayer("Countdown");
        const quint64 countdown = e.layerId(ci);
        require(e.setLayerClock(ci, ClockKind::Countdown), "a countdown");
        Animation timeline;
        timeline.name = "Lights";
        timeline.duration = 10;
        const quint64 tl = e.animation(e.addAnimation(timeline)).id;

        // What a preset can hold: the actions too
        auto info = [&](quint64 layer, const QString &path) {
            ParamInfo i;
            require(e.parameterInfo(layer, path, &i), qPrintable(path + " exists"));
            return i;
        };
        for (const auto &[layer, path] : std::vector<std::pair<quint64, QString>>{
                 {movie, "rewind"}, {movie, "seek"}, {countdown, "clock/start"}, {countdown, "clock/reset"},
                 {0, QStringLiteral("timeline/%1/play").arg(tl)}, {0, QStringLiteral("timeline/%1/stop").arg(tl)}, {0, "show/go"}}) {
            const ParamInfo i = info(layer, path);
            require(i.presettable && i.action && !i.stored, qPrintable(path + " is an action a preset can hold"));
        }
        require(!info(movie, "playhead").presettable, "a read-only value is not");
        require(info(movie, "opacity").presettable && !info(movie, "opacity").action, "a value is a value");

        // Captured: a trigger as "do it", Seek To as where the media is
        require(e.presetValue(movie, "rewind").value == QJsonValue(true), "Rewind is held as true");
        require(e.presetValue(movie, "seek").value.isDouble(), "Seek To is held as a time");

        // Recalled: done
        Engine::Preset p;
        p.name = "Go";
        p.fade = 0;
        p.values.push_back(e.presetValue(movie, "rewind"));
        p.values.push_back(e.presetValue(countdown, "clock/start"));
        p.values.push_back(e.presetValue(0, QStringLiteral("timeline/%1/play").arg(tl)));
        Engine::PresetValue seek = e.presetValue(movie, "seek");
        seek.value = 1.0;
        p.values.push_back(seek);
        const int pi = e.addPreset(p);
        require(!e.layerClock(ci)->running(), "the countdown waits");
        e.recallPreset(pi);
        for (int k = 0; k < 4; ++k) e.renderFrame();
        QCoreApplication::processEvents();
        require(e.layerClock(ci)->running(), "a preset starts a countdown");
        Engine::AnimationPlayback play;
        require(e.sampleAnimation(tl, 0, {}, &play) && play.state == AnimState::Playing, "a preset plays a timeline");
        double at = -1;
        QElapsedTimer seeking; // the decoder seeks on its own thread: slower machines take a few frames more
        seeking.start();
        while (!(e.animParamValue(movie, "playhead", &at) && std::abs(at - 1.0) < 0.3) && seeking.elapsed() < 5000) {
            e.renderFrame();
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        require(std::abs(at - 1.0) < 0.3, "a preset seeks a media (after its rewind)");
        // An ISF shader's Restart: its TIME from 0 again, held by a preset
        {
            const int si = e.addLayer("Shader");
            const quint64 shader = e.layerId(si);
            require(e.setLayerIsf(si, QStringLiteral(TEST_DIR) + "/../isf/generators/Basic Shape.fs", &err), qPrintable(err));
            QString restart;
            for (const ParamInfo &i : e.parameters(shader))
                if (i.path.endsWith(QLatin1String("restart")) && i.label.endsWith(QLatin1String("Restart")) && !i.path.startsWith("fx/")) restart = i.path;
            require(!restart.isEmpty(), "an ISF generator has a Restart");
            const ParamInfo ri = info(shader, restart);
            require(ri.action && ri.presettable, "Restart is an action a preset can hold");
            e.setFadesManual(false);
            for (int k = 0; k < 30; ++k) e.renderFrame();
            auto time = [&] { Engine::Lock lk(&e.mutex()); return e.layer(e.indexOfId(shader))->generator->time(); };
            const double before = time();
            require(before > 0, "the shader's time runs");
            Engine::Preset r;
            r.name = "Restart";
            r.fade = 0;
            r.values.push_back(e.presetValue(shader, restart));
            require(r.values.back().value == QJsonValue(true), "held as do it");
            e.recallPreset(e.addPreset(r));
            e.renderFrame();
            require(time() < before && time() < 0.2, "a preset restarts an ISF shader from its first frame");
        }
        e.shutdown();
        std::cout << "Preset actions: held, captured, recalled passed\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
