// How long a video takes to show its first picture after it is opened, and after a seek (a cue, a loop): the decoders alone.
// Usage: nyxmap_video_latency_bench file...
#include "VideoDecoder.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <cstdio>

static double firstFrame(VideoDecoder &d, double t, int timeoutMs = 3000)
{
    QElapsedTimer tm;
    tm.start();
    VideoFrame f;
    while (tm.elapsed() < timeoutMs) {
        if (d.fetch(t, f)) return tm.nsecsElapsed() / 1e6;
        QThread::usleep(200);
    }
    return -1;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    for (int i = 1; i < argc; ++i) {
        const QString file = QString::fromLocal8Bit(argv[i]);
        QElapsedTimer tm;
        tm.start();
        VideoDecoder d;
        QString err;
        if (!d.open(file, &err)) { std::printf("%s: %s\n", argv[i], qPrintable(err)); continue; }
        const double opened = tm.nsecsElapsed() / 1e6;
        Timeline t;
        t.mode = Timeline::Loop;
        d.setTimeline(t);
        d.seek(0);
        const double first = firstFrame(d, 0.0);
        std::printf("%-28s %-22s open %6.1f ms, first picture %6.1f ms after the seek", qPrintable(QFileInfo(file).fileName()), qPrintable(d.codecName()), opened, first);
        // Let it run a second, then jump: a cue seeking inside the file
        for (int k = 0; k < 30; ++k) { firstFrame(d, k / d.fps(), 200); }
        double worst = 0, sum = 0;
        const int jumps = 8;
        for (int k = 0; k < jumps; ++k) {
            const double target = 0.5 + k * 1.1;
            d.seek(target);
            const double ms = firstFrame(d, target);
            worst = std::max(worst, ms);
            sum += ms;
        }
        std::printf("; seeks: mean %.1f ms, worst %.1f ms\n", sum / jumps, worst);
    }
    return 0;
}
