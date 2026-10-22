#include "ClockGenerator.h"

#include <QLocale>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Kinds and settings
// ---------------------------------------------------------------------------
static const char *const kKindKeys[] = {"countdown", "stopwatch", "clock"};

QString clockKindKey(ClockKind k) { return QString::fromLatin1(kKindKeys[int(k)]); }

QString clockKindName(ClockKind k)
{
    switch (k) {
    case ClockKind::Countdown: return QStringLiteral("Countdown");
    case ClockKind::Stopwatch: return QStringLiteral("Stopwatch");
    case ClockKind::Clock: return QStringLiteral("Clock");
    }
    return {};
}

std::optional<ClockKind> clockKindFromKey(const QString &k)
{
    for (int i = 0; i < 3; ++i)
        if (k == QLatin1String(kKindKeys[i])) return ClockKind(i);
    return std::nullopt;
}

ClockSettings ClockSettings::defaults(ClockKind kind)
{
    ClockSettings s;
    s.kind = kind;
    s.format = kind == ClockKind::Countdown ? QStringLiteral("mm:ss")
               : kind == ClockKind::Stopwatch ? QStringLiteral("mm:ss.t")
                                              : QStringLiteral("24h");
    return s;
}

QStringList ClockGenerator::durationFormats()
{
    return {"hh:mm:ss", "mm:ss", "ss", "hh:mm:ss.t", "mm:ss.t", "ss.t", "hh:mm:ss:ff", "mm:ss:ff", "custom"};
}

QStringList ClockGenerator::clockFormats() { return {"24h", "12h", "custom"}; }

// ---------------------------------------------------------------------------
// Formats
// ---------------------------------------------------------------------------
QString ClockGenerator::formatDuration(double seconds, const QString &format, int fps, bool countDown)
{
    if (!std::isfinite(seconds)) seconds = 0;
    fps = std::clamp(fps, 1, 1000);
    // The format's tokens: runs of h, m, s, t, f (any case); a quoted text as it is; any other character as it is
    struct Token {
        QChar unit; // 0: a text
        int width = 0;
        QString text;
    };
    std::vector<Token> tokens;
    bool hasH = false, hasM = false, hasS = false, hasF = false;
    int decimals = 0;
    for (int i = 0; i < format.size();) {
        const QChar c = format[i].toLower();
        if (format[i] == QLatin1Char('\'')) { // 'text' ('' inside: a quote)
            QString text;
            int k = i + 1;
            for (; k < format.size(); ++k) {
                if (format[k] != QLatin1Char('\'')) {
                    text += format[k];
                } else if (k + 1 < format.size() && format[k + 1] == QLatin1Char('\'')) {
                    text += QLatin1Char('\'');
                    ++k;
                } else {
                    break;
                }
            }
            tokens.push_back({QChar(), 0, k == i + 1 && k < format.size() ? QStringLiteral("'") : text});
            i = k + 1;
            continue;
        }
        if (c == 'h' || c == 'm' || c == 's' || c == 't' || c == 'f') {
            int n = 1;
            while (i + n < format.size() && format[i + n].toLower() == c) ++n;
            if (c == 'h') hasH = true;
            if (c == 'm') hasM = true;
            if (c == 's') hasS = true;
            if (c == 'f') hasF = true;
            if (c == 't') decimals = std::max(decimals, std::min(n, 3));
            tokens.push_back({c, c == 't' ? std::min(n, 3) : n, QString()});
            i += n;
            continue;
        }
        if (!tokens.empty() && tokens.back().unit.isNull()) tokens.back().text += format[i];
        else tokens.push_back({QChar(), 0, QString(format[i])});
        ++i;
    }
    // Counted in ticks: frames, fractions of a second, or seconds; rounded to the smallest unit shown
    qint64 perSec = 1;
    if (hasF) perSec = fps;
    else for (int k = 0; k < decimals; ++k) perSec *= 10;
    const qint64 step = (hasF || decimals || hasS) ? 1 : hasM ? 60 : hasH ? 3600 : 1;
    const double ticks = std::min(std::abs(seconds), 1e12) * double(perSec) / double(step);
    const bool up = countDown && seconds > 0;
    const qint64 n = qint64(up ? std::ceil(ticks - 1e-6) : std::floor(ticks + 1e-6)) * step;
    const bool negative = seconds < 0 && n > 0;
    const qint64 sub = n % perSec; // ticks within the second
    qint64 s = n / perSec, h = 0, m = 0;
    // The largest unit shown holds all of the time above it (no hours shown: 90 minutes are 90 minutes)
    if (hasH) {
        h = s / 3600;
        s %= 3600;
    }
    if (hasM) {
        m = s / 60;
        s %= 60;
    }
    auto pad = [](qint64 v, int width) { return QStringLiteral("%1").arg(v, width, 10, QLatin1Char('0')); };
    QString out = negative ? QStringLiteral("-") : QString();
    for (const Token &t : tokens) {
        if (t.unit.isNull()) out += t.text;
        else if (t.unit == 'h') out += pad(h, t.width);
        else if (t.unit == 'm') out += pad(m, t.width);
        else if (t.unit == 's') out += pad(s, t.width);
        else if (t.unit == 'f') out += pad(hasF ? sub : 0, t.width);
        else { // t: the fraction of the second, as many digits as the token has
            qint64 scale = 1;
            for (int k = 0; k < t.width; ++k) scale *= 10;
            out += pad(sub * scale / perSec, t.width);
        }
    }
    return out;
}

QString ClockGenerator::formatTimeOfDay(const QDateTime &t, const ClockSettings &s)
{
    if (s.format == QLatin1String("custom") && !s.customFormat.isEmpty()) return QLocale::c().toString(t, s.customFormat);
    const bool h12 = s.format == QLatin1String("12h");
    const QString f = h12 ? (s.seconds ? QStringLiteral("h:mm:ss AP") : QStringLiteral("h:mm AP"))
                          : (s.seconds ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"));
    QString out = QLocale::c().toString(t, f);
    if (s.date) out += QLatin1Char('\n') + QLocale().toString(t.date(), QLocale::LongFormat);
    return out;
}

QTimeZone ClockGenerator::zoneOf(const QString &zone)
{
    const QString z = zone.trimmed();
    if (z.isEmpty()) return QTimeZone::systemTimeZone();
    if (!z.compare(QLatin1String("UTC"), Qt::CaseInsensitive) || !z.compare(QLatin1String("GMT"), Qt::CaseInsensitive))
        return QTimeZone::utc();
    // An offset from UTC: "+2", "-05:30", "UTC+01:00"
    static const QRegularExpression offset(QStringLiteral("^(?:UTC|GMT)?\\s*([+-])(\\d{1,2})(?::?(\\d{2}))?$"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = offset.match(z);
    if (m.hasMatch()) {
        const int secs = m.captured(2).toInt() * 3600 + m.captured(3).toInt() * 60;
        if (secs <= 14 * 3600) return QTimeZone(m.captured(1) == QLatin1String("-") ? -secs : secs);
    }
    const QTimeZone tz(z.toUtf8());
    return tz.isValid() ? tz : QTimeZone::systemTimeZone();
}

QTime ClockGenerator::parseTime(const QString &text)
{
    const QString t = text.trimmed().toUpper();
    for (const char *f : {"H:mm:ss", "H:mm", "h:mm:ss AP", "h:mm AP", "h:mm:ssAP", "h:mmAP", "H:mm:ss.zzz"}) {
        const QTime q = QLocale::c().toTime(t, QString::fromLatin1(f));
        if (q.isValid()) return q;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Generator
// ---------------------------------------------------------------------------
ClockGenerator::ClockGenerator(ClockKind kind) : m_kind(kind), m_s(ClockSettings::defaults(kind))
{
    m_timer.start();
    m_anchor = msLocked();
    aimLocked();
    m_frozen = leftToAimLocked();
}

qint64 ClockGenerator::msLocked() const { return m_testNow.isValid() ? m_testNow.toMSecsSinceEpoch() : m_timer.elapsed(); }

QDateTime ClockGenerator::nowLocked() const { return m_testNow.isValid() ? m_testNow : QDateTime::currentDateTime(); }

void ClockGenerator::setTestNow(const QDateTime &now)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    // Rebase only when changing clock domains. Advancing an already injected
    // clock must advance the transport from its existing anchor.
    const bool switchingClock = m_testNow.isValid() != now.isValid();
    const double e = switchingClock ? elapsedLocked() : 0;
    m_testNow = now;
    if (switchingClock) {
        m_base = e;
        m_anchor = msLocked();
    }
}

double ClockGenerator::elapsedLocked() const { return m_base + (m_running ? (msLocked() - m_anchor) / 1000.0 : 0.0); }

double ClockGenerator::leftToAimLocked() const { return m_aim.isValid() ? nowLocked().msecsTo(m_aim) / 1000.0 : 0.0; }

// The next time of day it counts down to, from now (now itself when it is that time)
void ClockGenerator::aimLocked()
{
    const QTimeZone zone = zoneOf(m_s.zone);
    const QDateTime now = nowLocked().toTimeZone(zone);
    const QTime t = m_s.targetTime.isValid() ? m_s.targetTime : QTime(0, 0);
    QDateTime aim(now.date(), t, zone);
    if (aim < now) aim = QDateTime(now.date().addDays(1), t, zone);
    m_aim = aim;
}

double ClockGenerator::valueLocked() const
{
    switch (m_kind) {
    case ClockKind::Countdown: {
        const double left = m_s.target == ClockSettings::Target::TimeOfDay ? (m_running ? leftToAimLocked() : m_frozen)
                                                                           : m_s.duration - elapsedLocked();
        return m_s.atZero == ClockSettings::AtZero::Overtime ? left : std::max(0.0, left);
    }
    case ClockKind::Stopwatch: return m_s.offset + elapsedLocked();
    case ClockKind::Clock: {
        const QDateTime now = nowLocked().toTimeZone(zoneOf(m_s.zone));
        return now.time().msecsSinceStartOfDay() / 1000.0;
    }
    }
    return 0;
}

bool ClockGenerator::finishedLocked() const { return m_kind == ClockKind::Countdown && valueLocked() <= 0; }

void ClockGenerator::resetLocked()
{
    m_base = 0;
    m_anchor = msLocked();
    aimLocked();
    m_frozen = leftToAimLocked();
}

ClockSettings ClockGenerator::settings() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_s;
}

void ClockGenerator::update(const std::function<void(ClockSettings &)> &fn)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    const ClockSettings before = m_s;
    fn(m_s);
    m_s.kind = m_kind;
    m_s.duration = std::clamp(std::isfinite(m_s.duration) ? m_s.duration : 0.0, 0.0, 100.0 * 24 * 3600);
    m_s.offset = std::clamp(std::isfinite(m_s.offset) ? m_s.offset : 0.0, -100.0 * 24 * 3600, 100.0 * 24 * 3600);
    m_s.fps = std::clamp(m_s.fps, 1, 1000);
    if (!m_s.targetTime.isValid()) m_s.targetTime = before.targetTime;
    if (m_s.target != before.target || m_s.targetTime != before.targetTime || m_s.zone != before.zone) {
        aimLocked(); // another time of day: aimed at from now
        if (!m_running) m_frozen = leftToAimLocked();
    }
}

bool ClockGenerator::running() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_running;
}

void ClockGenerator::setRunning(bool on)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!hasTransport()) return;
    if (on && finishedLocked() && m_s.atZero != ClockSettings::AtZero::Overtime) resetLocked(); // done: from the start
    if (on == m_running) return;
    if (on) {
        m_anchor = msLocked();
    } else {
        m_base = elapsedLocked();
        m_frozen = leftToAimLocked();
    }
    m_running = on;
}

void ClockGenerator::reset()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_running = false;
    resetLocked();
}

void ClockGenerator::restart()
{
    reset();
    setRunning(true);
}

double ClockGenerator::elapsed() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return elapsedLocked();
}

void ClockGenerator::setElapsed(double seconds)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_base = std::clamp(std::isfinite(seconds) ? seconds : 0.0, 0.0, 100.0 * 24 * 3600);
    m_anchor = msLocked();
}

double ClockGenerator::value() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return valueLocked();
}

bool ClockGenerator::finished() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return finishedLocked();
}

bool ClockGenerator::overtime() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_kind == ClockKind::Countdown && m_s.atZero == ClockSettings::AtZero::Overtime && valueLocked() < 0;
}

QString ClockGenerator::display() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_kind == ClockKind::Clock) return formatTimeOfDay(nowLocked().toTimeZone(zoneOf(m_s.zone)), m_s);
    const QString format = m_s.format == QLatin1String("custom") ? m_s.customFormat
                           : m_s.format.isEmpty()                 ? QStringLiteral("hh:mm:ss")
                                                                  : m_s.format;
    if (m_kind == ClockKind::Stopwatch) return formatDuration(valueLocked(), format, m_s.fps, false);
    if (finishedLocked() && m_s.atZero == ClockSettings::AtZero::Text) return m_s.zeroText;
    return formatDuration(valueLocked(), format, m_s.fps, true);
}

QColor ClockGenerator::color(const QColor &normal) const { return overtime() ? settings().overtimeColor : normal; }

QString ClockGenerator::status() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_kind == ClockKind::Clock) {
        const QTimeZone zone = zoneOf(m_s.zone);
        return m_s.zone.trimmed().isEmpty() ? QStringLiteral("System time zone (%1)").arg(QString::fromUtf8(zone.id()))
                                            : QString::fromUtf8(zone.id());
    }
    QString state = m_running ? QStringLiteral("Running") : QStringLiteral("Paused");
    if (m_kind == ClockKind::Countdown && valueLocked() <= 0)
        state = m_s.atZero == ClockSettings::AtZero::Overtime && valueLocked() < 0 ? QStringLiteral("Overtime")
                                                                                   : QStringLiteral("Finished");
    if (m_kind == ClockKind::Countdown && m_s.target == ClockSettings::Target::TimeOfDay && m_aim.isValid())
        state += QStringLiteral(" · to %1").arg(QLocale::c().toString(m_aim, QStringLiteral("ddd HH:mm:ss")));
    return state;
}
