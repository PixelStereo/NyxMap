#pragma once
// The status bar: one line, and one thing in it at a time: the message just given (it leaves after its time), else what the
// pointer is over. What it says is kept in the Console, and a button there counts the errors and warnings, and opens it.
#include <QStatusBar>
#include <QTimer>

class QLabel;
class QToolButton;

class StatusLine : public QStatusBar
{
    Q_OBJECT
public:
    explicit StatusLine(QWidget *parent = nullptr);

    // Shadow QStatusBar's: a message replaces the line (and never overlaps what is hovered), its level tells its colour,
    // and the Console keeps it. ms 0: until another comes
    void showMessage(const QString &text, int ms = 0);
    void clearMessage();
    void setHelp(const QString &text); // what the pointer is over: shown while there is no message
    QLabel *line() const { return m_line; }
    QToolButton *alertButton() const { return m_alerts; }
    QString shown() const; // what the line says now

signals:
    void alertsClicked();

private:
    void showLine();
    void updateAlerts();
    QLabel *m_line = nullptr;
    QToolButton *m_alerts = nullptr;
    QString m_message, m_help;
    int m_level = 0; // of the message
    QTimer m_expire;
};
