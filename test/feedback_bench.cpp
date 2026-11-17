// What the OSC feedback costs a tick (every 100 ms while it is on): a project loaded, the feedback's work done again and again.
// Usage: nyxmap_feedback_bench project.nyxmap [ticks]
#include "Engine.h"
#include "SequenceFeedback.h"
#include <QApplication>
#include <QElapsedTimer>
#include <algorithm>
#include <cstdio>
#include <vector>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc < 2) return 2;
    Engine e;
    QString err, warn;
    if (!e.initialize(&err) || !e.loadProject(QString::fromLocal8Bit(argv[1]), nullptr, &warn)) return 1;
    e.start();
    SequenceFeedback fb(&e);
    fb.setTargets({QStringLiteral("127.0.0.1:11999")});
    fb.setEnabled(true);
    const int ticks = argc > 2 ? QString(argv[2]).toInt() : 30;
    std::vector<double> ms;
    fb.sendNow(true); // the first: everything
    for (int k = 0; k < ticks; ++k) {
        QElapsedTimer t;
        t.start();
        for (int slice = 0; slice < 4; ++slice) fb.sendNow(false, slice, 4); // a cycle: the timer's four ticks
        ms.push_back(t.nsecsElapsed() / 1e6);
    }
    std::sort(ms.begin(), ms.end());
    double sum = 0;
    for (double v : ms) sum += v;
    std::printf("feedback cycle (4 slices): mean %.2f ms, median %.2f, p95 %.2f, max %.2f (%d ticks, every 100 ms)\n", sum / double(ms.size()), ms[ms.size() / 2],
                ms[ms.size() * 95 / 100], ms.back(), ticks);
    e.stop();
    return 0;
}
