// Decoder-only fidelity check: no GL context, output device or timing benchmark.
#include "VideoDecoder.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cmath>
#include <cstdio>
#include <vector>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    for (bool hardware : {false, true}) {
        VideoDecoder::setHardwareDecoding(hardware);
        VideoDecoder decoder;
        QString error;
        if (!decoder.open(QString(TEST_DIR) + "/media/fmt_yuva444p10.mov", &error)) {
            std::printf("FAIL decoder_alpha: %s\n", qPrintable(error));
            return 1;
        }
        std::vector<uint8_t> pixels;
        int width = 0, height = 0;
        QElapsedTimer timer;
        timer.start();
        while (!decoder.fetchRgba(.001, pixels, &width, &height) && timer.elapsed() < 5000)
            QThread::msleep(1);
        if (width < 4 || height < 1) {
            std::printf("FAIL decoder_alpha: no decoded picture\n");
            ++failures;
            continue;
        }
#ifdef __APPLE__
        if (hardware && (decoder.hardwareActive() || decoder.decodeThreads() <= 0)) {
            std::printf("FAIL decoder_alpha: ProRes 4444 must use the software alpha-preserving path\n");
            ++failures;
        }
#endif
        // make_media.sh encodes alpha = 40 + 200*x/width. Opaque hardware
        // output used to pass RGB checks while silently discarding this ramp.
        for (int x : {width / 8, width / 2, 7 * width / 8}) {
            const int actual = pixels[(size_t(height / 2) * width + x) * 4 + 3];
            const int expected = 40 + 200 * x / width;
            std::printf("decoder_alpha: hardware requested %d, active %d, x %d alpha %d expected %d\n",
                        hardware, decoder.hardwareActive(), x, actual, expected);
            if (std::abs(actual - expected) > 3) ++failures;
        }
    }
    return failures ? 1 : 0;
}
