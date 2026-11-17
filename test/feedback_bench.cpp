// What the OSC feedback costs: idle (nothing changes: nothing should run), and one parameter changing (what it takes to
// tell a client). Usage: nyxmap_feedback_bench project.nyxmap [changes]
#include "Engine.h"
#include "SequenceFeedback.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <ctime>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc < 2) return 2;
    Engine e;
    QString err, warn;
    if (!e.initialize(&err) || !e.loadProject(QString::fromLocal8Bit(argv[1]), nullptr, &warn)) return 1;
    SequenceFeedback fb(&e); // the render thread is not running: only the feedback is measured
    fb.setTargets({QStringLiteral("127.0.0.1:11999")});
    int sent = 0;
    QObject::connect(&fb, &SequenceFeedback::sent, [&](int n) { sent += n; });
    QElapsedTimer t;
    t.start();
    fb.setEnabled(true);
    std::printf("enabling (the baseline: the whole namespace read once): %.1f ms\n", t.nsecsElapsed() / 1e6);
    // Idle: the event loop turns for two seconds and nothing is asked of the CPU
    const std::clock_t c0 = std::clock();
    t.restart();
    while (t.elapsed() < 2000) QCoreApplication::processEvents(QEventLoop::AllEvents, 50), QThread::msleep(5);
    std::printf("idle for 2 s: %.2f ms of CPU, %d messages sent\n", 1000.0 * double(std::clock() - c0) / CLOCKS_PER_SEC, sent);
    // A parameter changes
    quint64 layer = 0;
    for (int i = 0; i < e.layerCount() && !layer; ++i) {
        Engine::Lock lk(&e.mutex());
        if (e.layer(i)->name.startsWith("Polished Gold")) layer = e.layer(i)->id;
    }
    const int changes = argc > 2 ? QString(argv[2]).toInt() : 200;
    sent = 0;
    t.restart();
    for (int k = 0; k < changes; ++k) {
        e.setParameter(layer, "model/position/x", 0.001 * k);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    const double ms = t.nsecsElapsed() / 1e6;
    std::printf("%d changes of one parameter: %.3f ms each (the change, the read, the datagram), %d messages sent\n", changes, ms / changes, sent);
    return 0;
}
