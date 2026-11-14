#include "StatusLine.h"

#include "AppConsole.h"

#include <QLabel>
#include <QToolButton>

StatusLine::StatusLine(QWidget *parent) : QStatusBar(parent)
{
    setSizeGripEnabled(false);
    m_line = new QLabel(this);
    m_line->setObjectName(QStringLiteral("contextHelpStatus"));
    m_line->setTextFormat(Qt::PlainText);
    m_line->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // cut short, never wider than its room
    QStatusBar::addWidget(m_line, 1);

    m_alerts = new QToolButton(this);
    m_alerts->setObjectName(QStringLiteral("statusAlerts"));
    m_alerts->setCursor(Qt::PointingHandCursor);
    m_alerts->setAutoRaise(true);
    m_alerts->hide();
    QStatusBar::addPermanentWidget(m_alerts);
    connect(m_alerts, &QToolButton::clicked, this, &StatusLine::alertsClicked);

    m_expire.setSingleShot(true);
    connect(&m_expire, &QTimer::timeout, this, &StatusLine::clearMessage);
    connect(&AppConsole::instance(), &AppConsole::changed, this, &StatusLine::updateAlerts, Qt::QueuedConnection);
    updateAlerts();
}

void StatusLine::showMessage(const QString &text, int ms)
{
    m_message = text.simplified();
    if (m_message.isEmpty()) {
        clearMessage();
        return;
    }
    const AppConsole::Level level = AppConsole::levelOfMessage(m_message);
    m_level = int(level);
    AppConsole::instance().add(level, QStringLiteral("App"), m_message);
    if (ms > 0) m_expire.start(ms);
    else m_expire.stop();
    showLine();
}

void StatusLine::clearMessage()
{
    m_expire.stop();
    m_message.clear();
    showLine();
}

void StatusLine::setHelp(const QString &text)
{
    m_help = text;
    showLine();
}

QString StatusLine::shown() const { return m_line->text(); }

void StatusLine::showLine()
{
    const QString text = m_message.isEmpty() ? m_help : m_message;
    if (m_line->text() != text) m_line->setText(text);
    m_line->setToolTip(m_message.isEmpty() ? QString() : m_message);
    // A message's level is its colour: an error is told apart from what is only said
    const char *colour = m_message.isEmpty() || m_level == AppConsole::Info ? "" : (m_level == AppConsole::Error ? "color:#ff8f82;" : "color:#f2b84b;");
    m_line->setStyleSheet(QString::fromLatin1(colour));
}

void StatusLine::updateAlerts()
{
    const int errors = AppConsole::instance().count(AppConsole::Error), warnings = AppConsole::instance().count(AppConsole::Warning);
    if (!errors && !warnings) {
        m_alerts->hide();
        return;
    }
    QStringList parts;
    if (errors) parts << QStringLiteral("%1 error%2").arg(errors).arg(errors > 1 ? "s" : "");
    if (warnings) parts << QStringLiteral("%1 warning%2").arg(warnings).arg(warnings > 1 ? "s" : "");
    m_alerts->setText(QStringLiteral("● ") + parts.join(QStringLiteral(" · ")));
    m_alerts->setToolTip(QStringLiteral("Open the Console: every message, with the errors and warnings"));
    const bool bad = errors > 0;
    m_alerts->setStyleSheet(QStringLiteral("QToolButton { color:%1; background:%2; border:1px solid %1; border-radius:9px; padding:1px 9px; }"
                                           "QToolButton:hover { background:%3; }")
                                .arg(bad ? "#ff8f82" : "#f2b84b", bad ? "#3a1512" : "#3a2a12", bad ? "#4a1c18" : "#4a3616"));
    m_alerts->show();
}
