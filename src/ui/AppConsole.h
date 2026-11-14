#pragma once
// The application's messages, kept for the Console: what the status bar said, and what Qt and the engine warned about. A
// bounded ring, written from any thread, read by the interface (the Console panel and the alert button of the status bar).
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <atomic>
#include <mutex>
#include <vector>

class AppConsole : public QObject
{
    Q_OBJECT
public:
    enum Level { Info = 0, Warning, Error };
    struct Entry {
        quint64 seq = 0; // increasing, one per message
        qint64 time = 0; // ms since the epoch
        Level level = Info;
        QString source;  // "App": what the interface said; "Qt": what the libraries warned about; "Console": a command
        QString text;
    };

    explicit AppConsole(int capacity = 2000, QObject *parent = nullptr);
    static AppConsole &instance(); // the application's

    // Any thread
    void add(Level level, const QString &source, const QString &text);
    void clear();

    std::vector<Entry> entries() const; // oldest first
    int count(Level level) const;       // of those kept
    quint64 generation() const { return m_generation.load(std::memory_order_acquire); } // changes on every change

    // A status message is told from its words (no level is given): "could not…", "failed", "not available" are errors
    static Level levelOfMessage(const QString &text);
    static QString levelName(Level level); // "Info", "Warning", "Error"

signals:
    void changed(); // queued: from any thread to the interface's

private:
    int m_capacity;
    mutable std::mutex m_mutex;
    std::vector<Entry> m_rows;
    quint64 m_nextSeq = 1;
    std::atomic<quint64> m_generation{0};
};
