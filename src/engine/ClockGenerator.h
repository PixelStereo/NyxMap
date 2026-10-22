#pragma once
// The Countdown, Stopwatch and Clock generators: text layers that show a time, as the Timecode generator shows a
// timecode (its text style is the Text generator's: font, size, color, alignment, outline, shadow).
//  - Countdown: from a duration down to zero, or down to a time of day (20:30:00, the next one to come). At zero it
//    holds there, shows a text of its own ("00:00", "END"…), or goes on into overtime (negative, in a color of its own);
//  - Stopwatch: counts up from zero, or from an offset;
//  - Clock: the time of day, 24 h or 12 h, with or without its seconds and its date, in a time zone of its own.
// The countdown and the stopwatch run with real time (the composition's speed does not change them), started, paused
// and reset by their own transport (parameters clock/play, clock/start, clock/pause, clock/reset: the inspector, OSC,
// interactions, snapshots, presets); STOP ALL pauses them as it pauses the media.
// Its settings and its transport are the layer's parameters (clock/*, LayerParams.cpp): saved with the layer's values,
// held by snapshots and presets. The layer's source says which kind it is.
// Thread safe: the render thread reads, the interface and OSC write.

#include <QColor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QTimeZone>
#include <functional>
#include <mutex>
#include <optional>

enum class ClockKind { Countdown = 0, Stopwatch, Clock };
QString clockKindKey(ClockKind k);  // "countdown", "stopwatch", "clock": in the layer's source
QString clockKindName(ClockKind k); // "Countdown", "Stopwatch", "Clock"
std::optional<ClockKind> clockKindFromKey(const QString &k);

struct ClockSettings {
    enum class Target { Duration = 0, TimeOfDay }; // a countdown: from a duration, or to a time of day
    enum class AtZero { Stop = 0, Text, Overtime }; // what a countdown shows once at zero
    ClockKind kind = ClockKind::Countdown;
    Target target = Target::Duration;
    double duration = 300;                 // seconds (countdown from a duration)
    QTime targetTime = QTime(20, 30, 0);   // countdown to a time of day
    double offset = 0;                     // stopwatch: where it starts, in seconds
    // Countdown, stopwatch: one of durationFormats() ("hh:mm:ss"…) or "custom" (customFormat, its tokens: h hh, m mm,
    // s ss, t tt ttt (tenths, hundredths, thousandths), ff (frames at `fps`), 'text' quoted as it is; any case).
    // Clock: "24h", "12h" or "custom" (customFormat as a Qt date-time format: "HH:mm:ss", "dddd d MMMM"…)
    QString format;
    QString customFormat;
    int fps = 25;                          // the frames of the ff token
    AtZero atZero = AtZero::Stop;
    QString zeroText = QStringLiteral("00:00");
    QColor overtimeColor = QColor(255, 64, 64);
    bool seconds = true, date = false;     // clock: its seconds shown; its date under it
    QString zone; // clock, countdown to a time of day: "" the system's, "UTC", an IANA id ("Europe/Paris"), "+02:00"

    static ClockSettings defaults(ClockKind kind);
};

class ClockGenerator
{
public:
    explicit ClockGenerator(ClockKind kind);
    ClockKind kind() const { return m_kind; }
    bool hasTransport() const { return m_kind != ClockKind::Clock; }

    ClockSettings settings() const;
    // Its settings changed (a countdown's target, its time or its zone: it aims at that time again)
    void update(const std::function<void(ClockSettings &)> &fn);

    // Transport (countdown, stopwatch). Started at its end (a countdown at zero that does not go on): from the start
    bool running() const;
    void setRunning(bool on);
    void start() { setRunning(true); }
    void pause() { setRunning(false); }
    void reset();   // back to its start (its duration, its offset; a time of day aimed at again), paused
    void restart(); // back to its start, running
    double elapsed() const; // seconds it ran since its start (a countdown from a duration, a stopwatch)
    void setElapsed(double seconds);

    // What it shows: a countdown's time left (negative: overtime), a stopwatch's time, the clock's seconds since midnight
    double value() const;
    bool finished() const; // a countdown at zero (or past it)
    bool overtime() const; // a countdown past zero, going on
    QString display() const;
    QColor color(const QColor &normal) const; // the text's color, or the overtime's
    QString status() const;                   // "Running", "Paused", "Finished", "Overtime", the zone of a clock

    // Formats
    static QStringList durationFormats(); // the countdown's and the stopwatch's, then "custom"
    static QStringList clockFormats();    // "24h", "12h", "custom"
    // A time in seconds as `format` lays it out (above). Counting down, a time is rounded up to what the format shows
    // (it reaches 00:00 at zero, not a second before); counting up, down. Negative: a minus before it
    static QString formatDuration(double seconds, const QString &format, int fps = 25, bool countDown = false);
    static QString formatTimeOfDay(const QDateTime &t, const ClockSettings &s);
    static QTimeZone zoneOf(const QString &zone); // the system's when it is empty or not known
    static QTime parseTime(const QString &text);  // "20:30", "20:30:00", "8:30 PM" (invalid: none of these)

    void setTestNow(const QDateTime &now); // tests: the time it is now, its transport with it (invalid: real time)

private:
    const ClockKind m_kind;
    mutable std::mutex m_mutex;
    ClockSettings m_s;
    QElapsedTimer m_timer;
    QDateTime m_testNow;
    bool m_running = false;
    double m_base = 0;     // elapsed when it last paused or was set
    qint64 m_anchor = 0;   // msLocked() when it last started (or was set while running)
    QDateTime m_aim;       // a countdown to a time of day: that time, the next one after it was aimed at
    double m_frozen = 0;   // the same, paused: the time that was left

    qint64 msLocked() const;
    QDateTime nowLocked() const;
    double elapsedLocked() const;
    double valueLocked() const;
    double leftToAimLocked() const; // a countdown to a time of day, running
    bool finishedLocked() const;
    void aimLocked();
    void resetLocked();
};
