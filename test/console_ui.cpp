// The status bar says one thing at a time and keeps what it said; the Console shows it all, filtered by level, source and
// text, with a command line; the alert button counts the errors and warnings and opens it.
#include "AppConsole.h"
#include "ConsolePanel.h"
#include "Engine.h"
#include "StatusLine.h"
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QTableView>
#include <QToolButton>
#include <cstdio>

int runConsoleUiTests(Engine &)
{
    int failures = 0;
    auto check = [&](bool ok, const char *reason) {
        if (!ok) { ++failures; std::printf("FAIL console_ui: %s\n", reason); }
    };
    auto pump = [] {
        for (int k = 0; k < 4; ++k) QApplication::processEvents();
    };
    AppConsole console;
    // The level of a message, told from its words
    check(AppConsole::levelOfMessage("Remote port 8765 is not available (address already in use)") == AppConsole::Error, "an unavailable port is an error");
    check(AppConsole::levelOfMessage("Corner moved · Test Pattern") == AppConsole::Info, "what is only said is information");
    check(AppConsole::levelOfMessage("Warning: the file is large") == AppConsole::Warning, "a warning is a warning");

    // The ring: oldest first, bounded, counted by level
    {
        AppConsole small(10);
        for (int i = 0; i < 25; ++i) small.add(i % 5 == 0 ? AppConsole::Error : AppConsole::Info, "Test", QString::number(i));
        const auto rows = small.entries();
        check(rows.size() == 10 && rows.front().text == "15" && rows.back().text == "24", "the oldest messages leave the ring");
        check(small.count(AppConsole::Error) == 2, "counted by level");
        small.clear();
        check(small.entries().empty() && small.count(AppConsole::Error) == 0, "clear forgets them");
    }

    // The status line: a message replaces the line, what is hovered comes back after it, the Console keeps it
    AppConsole::instance().clear();
    StatusLine status;
    status.resize(900, 24);
    status.show();
    status.setHelp("Hovered help");
    check(status.shown() == "Hovered help", "the line shows what is hovered");
    status.showMessage("Saved the project", 60000);
    check(status.shown() == "Saved the project", "a message replaces it: one thing at a time");
    status.setHelp("Other help");
    check(status.shown() == "Saved the project", "what is hovered does not overlap a message");
    status.clearMessage();
    check(status.shown() == "Other help", "and comes back when the message is gone");
    check(!status.alertButton()->isVisibleTo(&status), "no alert button while there is no error");
    status.showMessage("Remote port 8765 is not available", 0);
    status.showMessage("Could not open the file", 0);
    AppConsole::instance().add(AppConsole::Warning, "Qt", "QObject::startTimer: odd");
    pump();
    check(status.alertButton()->isVisibleTo(&status) && status.alertButton()->text().contains("2 errors") &&
              status.alertButton()->text().contains("1 warning"),
          "the alert button counts the errors and warnings");
    bool opened = false;
    QObject::connect(&status, &StatusLine::alertsClicked, [&] { opened = true; });
    status.alertButton()->click();
    check(opened, "and asks for the Console");

    // The Console: the same messages, filtered
    ConsolePanel panel;
    panel.resize(900, 300);
    panel.show();
    pump();
    check(panel.shownCount() == 4, "the Console lists every message");
    if (const QString captures = qEnvironmentVariable("NYXMAP_UI_CAPTURE_DIR"); !captures.isEmpty()) {
        AppConsole::instance().add(AppConsole::Info, "App", "Corner moved · Test Pattern · ⌘Z to undo");
        AppConsole::instance().add(AppConsole::Info, "OSC", "/nyxmap/layer/Spiral/opacity 0.9");
        pump();
        panel.grab().save(captures + "/console.png");
        status.grab().save(captures + "/status-line.png");
    }
    panel.setLevelFilter("errors");
    check(panel.shownCount() >= 2, "the Errors button shows the errors only");
    panel.setLevelFilter("warnings");
    check(panel.shownCount() == 1, "and the Warnings button the warnings");
    panel.setLevelFilter("all");
    if (auto *search = panel.findChild<QLineEdit *>("consoleSearch")) {
        search->setText("port");
        pump();
        check(panel.shownCount() == 1, "a text keeps the messages that hold it");
        search->clear();
    }
    panel.setLevelFilter("all");
    // The command line
    panel.runCommand("help");
    panel.runCommand("nonsense");
    pump();
    check(AppConsole::instance().count(AppConsole::Error) == 3, "an unknown command is an error, and says scripts are not there yet");
    panel.runCommand("filter errors");
    pump();
    check(panel.shownCount() >= 3, "filter errors is a command too");
    panel.runCommand("clear");
    pump();
    check(AppConsole::instance().entries().empty() && !status.alertButton()->isVisibleTo(&status), "clear forgets everything, and the alert button goes");
    return failures;
}
