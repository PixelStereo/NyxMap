#include "AppConsole.h"

#include <QDateTime>
#include <algorithm>

AppConsole::AppConsole(int capacity, QObject *parent) : QObject(parent), m_capacity(std::max(10, capacity)) {}

AppConsole &AppConsole::instance()
{
    static AppConsole *console = new AppConsole; // lives to the end of the process: messages can come from any thread, late
    return *console;
}

void AppConsole::add(Level level, const QString &source, const QString &text)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Entry e;
        e.seq = m_nextSeq++;
        e.time = QDateTime::currentMSecsSinceEpoch();
        e.level = level;
        e.source = source;
        e.text = text;
        m_rows.push_back(std::move(e));
        if (int(m_rows.size()) > m_capacity) m_rows.erase(m_rows.begin(), m_rows.begin() + (m_rows.size() - size_t(m_capacity)));
    }
    m_generation.fetch_add(1, std::memory_order_release);
    emit changed();
}

void AppConsole::clear()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rows.clear();
    }
    m_generation.fetch_add(1, std::memory_order_release);
    emit changed();
}

std::vector<AppConsole::Entry> AppConsole::entries() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rows;
}

int AppConsole::count(Level level) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return int(std::count_if(m_rows.begin(), m_rows.end(), [level](const Entry &e) { return e.level == level; }));
}

AppConsole::Level AppConsole::levelOfMessage(const QString &text)
{
    const QString t = text.toLower();
    static const char *const errors[] = {"could not", "couldn't", "cannot", "can't", "failed", "unable", "not available", "not found", "error",
                                         "refused", "invalid", "missing", "no such", "denied", "in use"};
    for (const char *w : errors)
        if (t.contains(QLatin1String(w))) return Error;
    if (t.contains(QLatin1String("warning")) || t.contains(QLatin1String("ignored"))) return Warning;
    return Info;
}

QString AppConsole::levelName(Level level)
{
    return level == Error ? QStringLiteral("Error") : level == Warning ? QStringLiteral("Warning") : QStringLiteral("Info");
}
