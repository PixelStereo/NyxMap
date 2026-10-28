// A virtual MIDI output (the timecode's MTC to other software): made, seen by others, sent to, without an error.
#include "Midi.h"

#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (!midi::available() || !midi::virtualPorts()) {
        std::printf("midi_virtual: skipped (no virtual MIDI ports here)\n");
        return 0;
    }
#ifdef Q_OS_LINUX
    if (!QFile::exists(QStringLiteral("/dev/snd/seq"))) { // no ALSA sequencer (CI containers)
        std::printf("midi_virtual: skipped (no ALSA sequencer)\n");
        return 0;
    }
#endif
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::printf("FAIL midi_virtual: %s\n", why);
        }
    };
    const QString name = QStringLiteral("NyxMap virtual test %1").arg(QCoreApplication::applicationPid());
    midi::Output out;
    QString err;
    check(out.openVirtual(name, &err), qPrintable(QStringLiteral("a virtual port is made (%1)").arg(err)));
    check(err.isEmpty() && out.name() == name, "no error reported, its name kept");
    bool seen = false;
    for (int i = 0; i < 50 && !seen; ++i) { // other software sees it as a port to receive from
        seen = midi::inputNames().join('\n').contains(name);
        if (!seen) QThread::msleep(20);
    }
    check(seen, "other software sees it");
    out.send({0xF1, 0x00}); // an MTC quarter frame
    std::printf("midi_virtual: %s (%d failure%s)\n", failures ? "FAIL" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
