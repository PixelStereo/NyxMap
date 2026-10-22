// The Countdown, Stopwatch and Clock generators: their formats, their transport on a time given by the test (no
// waiting), a countdown to a time of day across midnight, their values in a layer's JSON, a snapshot's recall and STOP ALL.
#include "ClockGenerator.h"
#include "Engine.h"
#include "Parameter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
#include <cstdio>

int runClockGeneratorTests(Engine &e)
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::printf("FAIL clocks: %s\n", why);
        } else {
            std::printf("ok    clocks: %s\n", why);
        }
    };
    auto eq = [&](const QString &got, const char *want, const char *why) {
        if (got != QLatin1String(want)) std::printf("      got \"%s\", want \"%s\"\n", qPrintable(got), want);
        check(got == QLatin1String(want), why);
    };
    using G = ClockGenerator;

    // Formats: a countdown rounds up (it shows 00:00 at zero, not a second before), a stopwatch down
    eq(G::formatDuration(90, "mm:ss", 25, true), "01:30", "countdown 90 s as mm:ss");
    eq(G::formatDuration(89.2, "mm:ss", 25, true), "01:30", "countdown rounds up to the second shown");
    eq(G::formatDuration(89.2, "mm:ss", 25, false), "01:29", "stopwatch rounds down");
    eq(G::formatDuration(90, "hh:mm:ss", 25, true), "00:01:30", "hh:mm:ss");
    eq(G::formatDuration(90, "ss", 25, true), "90", "ss alone holds all the seconds");
    eq(G::formatDuration(5400, "mm:ss", 25, true), "90:00", "mm:ss holds all the minutes");
    eq(G::formatDuration(3661, "HH:MM:SS", 25, false), "01:01:01", "tokens in capitals");
    eq(G::formatDuration(5.25, "mm:ss.t", 25, false), "00:05.2", "tenths counting up");
    eq(G::formatDuration(5.25, "mm:ss.t", 25, true), "00:05.3", "tenths counting down");
    eq(G::formatDuration(1.5, "ss:ff", 25, false), "01:12", "frames at 25 fps");
    eq(G::formatDuration(-12, "mm:ss", 25, true), "-00:12", "overtime: negative, with a minus");
    eq(G::formatDuration(-0.4, "mm:ss", 25, true), "00:00", "less than a second of overtime: no minus yet");
    eq(G::formatDuration(0, "mm:ss", 25, true), "00:00", "zero");
    eq(G::formatDuration(3700, "'T-'h'h'mm", 25, false), "T-1h01", "custom tokens with quoted text");
    eq(G::formatDuration(42, "'It''s' ss", 25, false), "It's 42", "a quote inside a quoted text");

    const QDateTime t0(QDate(2026, 10, 21), QTime(23, 0, 0), QTimeZone::utc());
    auto at = [&t0](double secs) { return t0.addMSecs(qint64(std::llround(secs * 1000))); };

    { // Countdown from a duration: start, pause, reset, and what it does at zero
        G c(ClockKind::Countdown);
        c.setTestNow(at(0));
        c.update([](ClockSettings &s) { s.duration = 90; });
        eq(c.display(), "01:30", "countdown starts paused at its duration");
        check(!c.running() && !c.finished(), "paused, not finished");
        c.start();
        c.setTestNow(at(30));
        eq(c.display(), "01:00", "30 s later");
        c.pause();
        c.setTestNow(at(60));
        eq(c.display(), "01:00", "held while paused");
        check(std::abs(c.elapsed() - 30) < 1e-6, "elapsed 30 s");
        c.start();
        c.setTestNow(at(150));
        eq(c.display(), "00:00", "stops at zero");
        check(c.finished() && !c.overtime() && std::abs(c.value()) < 1e-9, "finished, value held at 0");
        c.update([](ClockSettings &s) {
            s.atZero = ClockSettings::AtZero::Text;
            s.zeroText = QStringLiteral("END");
        });
        eq(c.display(), "END", "a text at zero");
        c.update([](ClockSettings &s) { s.atZero = ClockSettings::AtZero::Overtime; });
        eq(c.display(), "-00:30", "overtime goes on below zero");
        check(c.overtime() && c.color(Qt::white) == c.settings().overtimeColor, "overtime in its own color");
        c.update([](ClockSettings &s) { s.atZero = ClockSettings::AtZero::Stop; });
        c.pause();
        c.start(); // finished: from the start again
        check(c.running() && std::abs(c.elapsed()) < 1e-6, "started at zero: from its duration again");
        eq(c.display(), "01:30", "full duration again");
        c.reset();
        check(!c.running() && std::abs(c.elapsed()) < 1e-6, "reset: back to the start, paused");
    }

    { // Countdown to a time of day, across midnight (23:00 UTC to 01:00: two hours)
        G c(ClockKind::Countdown);
        c.setTestNow(at(0));
        c.update([](ClockSettings &s) {
            s.zone = QStringLiteral("UTC");
            s.format = QStringLiteral("hh:mm:ss");
            s.target = ClockSettings::Target::TimeOfDay;
            s.targetTime = QTime(1, 0, 0);
        });
        eq(c.display(), "02:00:00", "to 01:00 from 23:00: across midnight");
        c.start();
        c.setTestNow(at(3600));
        eq(c.display(), "01:00:00", "an hour later");
        c.update([](ClockSettings &s) { s.targetTime = QTime(23, 30, 0); }); // aimed at again from now (00:00)
        eq(c.display(), "23:30:00", "another time: aimed at again from now");
        check(G::parseTime("20:30").isValid() && G::parseTime("8:30 PM") == QTime(20, 30) && !G::parseTime("x").isValid(),
              "times typed");
        check(G::zoneOf("+02:00").offsetFromUtc(t0) == 7200 && G::zoneOf("UTC").offsetFromUtc(t0) == 0,
              "time zones: an offset, UTC");
    }

    { // Stopwatch from an offset
        G w(ClockKind::Stopwatch);
        w.setTestNow(at(0));
        w.update([](ClockSettings &s) {
            s.offset = 10;
            s.format = QStringLiteral("mm:ss");
        });
        eq(w.display(), "00:10", "stopwatch at its offset");
        w.start();
        w.setTestNow(at(65));
        eq(w.display(), "01:15", "counts up");
        w.pause();
        w.setTestNow(at(100));
        eq(w.display(), "01:15", "held while paused");
        w.reset();
        eq(w.display(), "00:10", "reset to its offset");
        check(!w.running(), "reset: paused");
    }

    { // Clock: 24 h, 12 h, without seconds, another zone, a custom format
        G k(ClockKind::Clock);
        k.setTestNow(QDateTime(QDate(2026, 10, 21), QTime(13, 5, 9), QTimeZone::utc()));
        k.update([](ClockSettings &s) { s.zone = QStringLiteral("UTC"); });
        eq(k.display(), "13:05:09", "24 h");
        k.update([](ClockSettings &s) { s.format = QStringLiteral("12h"); });
        eq(k.display(), "1:05:09 PM", "12 h");
        k.update([](ClockSettings &s) {
            s.format = QStringLiteral("24h");
            s.seconds = false;
        });
        eq(k.display(), "13:05", "without its seconds");
        k.update([](ClockSettings &s) {
            s.seconds = true;
            s.zone = QStringLiteral("+02:00");
        });
        eq(k.display(), "15:05:09", "another time zone");
        k.update([](ClockSettings &s) {
            s.format = QStringLiteral("custom");
            s.customFormat = QStringLiteral("HH'h'mm");
        });
        eq(k.display(), "15h05", "custom format");
        k.start();
        check(!k.running() && !k.hasTransport(), "a clock has no transport");
    }

    // In a layer: its parameters, its JSON, a snapshot's recall, STOP ALL
    e.newProject();
    e.setFadesManual(true);
    const int li = e.addLayer(QStringLiteral("Countdown"));
    const quint64 id = e.layerId(li);
    check(e.setLayerClock(li, ClockKind::Countdown), "countdown generator on a layer");
    std::shared_ptr<ClockGenerator> c = e.layerClock(li);
    check(c && c->kind() == ClockKind::Countdown, "its generator");
    if (!c) return failures;
    c->setTestNow(at(0));
    {
        Engine::Lock lk(&e.mutex());
        Layer *l = e.layer(li);
        check(l && l->isText() && l->parameter("clock/start") && l->parameter("clock/pause") && l->parameter("clock/reset") &&
                  l->parameter("clock/play") && l->parameter("clock/duration") && l->parameter("clock/format") &&
                  l->parameter("text/font") && l->parameter("clock/value") && !l->parameter("clock/value")->isStored(),
              "its parameters (and the text's)");
    }
    check(e.setParameter(id, "clock/duration", 90) && e.setParameter(id, "clock/format", "mm:ss"), "duration and format set");
    check(e.setParameter(id, "clock/start", true) && c->running(), "clock/start runs it");
    c->setTestNow(at(20));
    check(e.setParameter(id, "clock/pause", true) && !c->running(), "clock/pause");
    eq(c->display(), "01:10", "20 s run");

    // JSON: its kind in the source, its settings and transport in the values; a copy made from it is the same
    const QJsonObject j = e.layerJson(li);
    check(j.value("source").toObject().value("clock").toString() == "countdown", "kind saved in the source");
    const QJsonObject params = j.value("params").toObject();
    check(params.value("clock/duration").toDouble() == 90 && params.value("clock/format").toString() == "mm:ss" &&
              std::abs(params.value("clock/elapsed").toDouble() - 20) < 1e-6 && !params.value("clock/play").toBool(true) &&
              !params.contains("clock/value"),
          "settings and transport saved in the values");
    const int copy = e.insertLayerJson(e.layerCount(), j);
    const std::shared_ptr<ClockGenerator> c2 = e.layerClock(copy);
    check(c2 && c2->kind() == ClockKind::Countdown && c2->settings().duration == 90 && !c2->running() &&
              std::abs(c2->elapsed() - 20) < 1e-6,
          "restored from its JSON, paused where it was");

    // A snapshot holds its settings and its transport
    Engine::Snapshot snap;
    snap.name = QStringLiteral("Countdown at 1:10");
    snap.fade = 0;
    snap.layers = e.captureLayers();
    const int si = e.addSnapshot(snap);
    check(e.setParameter(id, "clock/duration", 300) && e.setParameter(id, "clock/reset", true), "changed after the snapshot");
    e.recallSnapshot(si);
    e.advanceFades(0.01);
    c = e.layerClock(e.indexOfId(id));
    check(c && c->settings().duration == 90 && !c->running() && std::abs(c->elapsed() - 20) < 1e-6,
          "snapshot recalls duration, elapsed and paused state");

    // STOP ALL pauses it
    if (c) {
        c->start();
        check(c->running(), "running before STOP ALL");
        e.stopShowPlayback();
        check(!c->running(), "STOP ALL pauses it");
    }

    // Another kind: made again, its text style kept
    {
        Engine::Lock lk(&e.mutex());
        e.layer(e.indexOfId(id))->text.size = 123;
    }
    check(e.setLayerClock(e.indexOfId(id), ClockKind::Stopwatch), "stopwatch instead");
    {
        Engine::Lock lk(&e.mutex());
        const Layer *l = e.layer(e.indexOfId(id));
        check(l && l->clockGen && l->clockGen->kind() == ClockKind::Stopwatch && l->text.size == 123,
              "a stopwatch now, same text style");
    }
    return failures;
}
