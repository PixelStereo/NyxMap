#include "ConsolePanel.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

// The rows the filters let through
class ConsoleModel final : public QAbstractTableModel
{
public:
    explicit ConsoleModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 4; }
    QVariant headerData(int section, Qt::Orientation o, int role) const override
    {
        static const char *const names[] = {"Time", "Level", "Source", "Message"};
        return o == Qt::Horizontal && role == Qt::DisplayRole ? QVariant(QLatin1String(names[section])) : QVariant();
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= int(m_rows.size())) return {};
        const AppConsole::Entry &e = m_rows[size_t(index.row())];
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            switch (index.column()) {
            case 0: return QDateTime::fromMSecsSinceEpoch(e.time).toString(QStringLiteral("HH:mm:ss"));
            case 1: return AppConsole::levelName(e.level).toUpper();
            case 2: return e.source;
            default: return e.text;
            }
        }
        if (role == Qt::ForegroundRole)
            return index.column() == 1 || e.level != AppConsole::Info
                       ? QVariant(QColor(e.level == AppConsole::Error ? "#ff8f82" : e.level == AppConsole::Warning ? "#f2b84b" : "#7c7c86"))
                       : QVariant();
        if (role == Qt::BackgroundRole && e.level == AppConsole::Error) return QColor(0x2a, 0x16, 0x14);
        return {};
    }
    void setRows(std::vector<AppConsole::Entry> rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }
    const std::vector<AppConsole::Entry> &rows() const { return m_rows; }

private:
    std::vector<AppConsole::Entry> m_rows;
};

ConsolePanel::ConsolePanel(AppConsole *console, QWidget *parent) : QWidget(parent), m_console(console ? console : &AppConsole::instance())
{
    setObjectName(QStringLiteral("consolePanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(6);
    m_levels = new QButtonGroup(this);
    m_levels->setExclusive(true);
    struct Pill { const char *name; const char *colour; };
    static const Pill pills[4] = {{"All", "#9fb0d0"}, {"Errors", "#ff8f82"}, {"Warnings", "#f2b84b"}, {"Info", "#7c9cc0"}};
    for (int i = 0; i < 4; ++i) {
        auto *button = new QToolButton(this);
        button->setObjectName(QStringLiteral("consoleLevel%1").arg(QLatin1String(pills[i].name)));
        button->setCheckable(true);
        button->setChecked(i == 0);
        button->setText(QLatin1String(pills[i].name));
        const QString c = QLatin1String(pills[i].colour);
        button->setStyleSheet(QStringLiteral("QToolButton { background:#35353b; color:%1; border:1px solid #4a4a52; border-radius:11px; padding:3px 12px; font-weight:600; }"
                                             "QToolButton:hover { background:#404048; }"
                                             "QToolButton:checked { background:%1; color:#16161b; }").arg(c));
        m_levels->addButton(button, i);
        m_levelButtons[i] = button;
        bar->addWidget(button);
    }
    m_source = new QComboBox(this);
    m_source->setObjectName(QStringLiteral("consoleSource"));
    m_source->addItem(QStringLiteral("Source: all"));
    m_source->setToolTip(QStringLiteral("Where the messages come from"));
    bar->addWidget(m_source);
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("consoleSearch"));
    m_search->setPlaceholderText(QStringLiteral("Filter…"));
    m_search->setClearButtonEnabled(true);
    bar->addWidget(m_search, 1);
    auto *copy = new QPushButton(QStringLiteral("Copy"), this);
    copy->setToolTip(QStringLiteral("Copy the messages shown"));
    auto *clear = new QPushButton(QStringLiteral("Clear"), this);
    clear->setToolTip(QStringLiteral("Forget every message"));
    bar->addWidget(copy);
    bar->addWidget(clear);
    layout->addLayout(bar);

    m_model = new ConsoleModel(this);
    m_view = new QTableView(this);
    m_view->setObjectName(QStringLiteral("consoleTable"));
    m_view->setModel(m_model);
    m_view->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_view->verticalHeader()->hide();
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 6);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setWordWrap(false);
    m_view->setShowGrid(false);
    layout->addWidget(m_view, 1);

    auto *line = new QHBoxLayout;
    auto *prompt = new QLabel(QStringLiteral("›"), this);
    prompt->setStyleSheet(QStringLiteral("color:#3fbf6f; font-weight:700;"));
    m_command = new QLineEdit(this);
    m_command->setObjectName(QStringLiteral("consoleCommand"));
    m_command->setPlaceholderText(QStringLiteral("A command: help, clear, filter errors — scripts will come here"));
    auto *run = new QPushButton(QStringLiteral("Run"), this);
    m_summary = new QLabel(this);
    m_summary->setStyleSheet(QStringLiteral("color:#888;"));
    line->addWidget(prompt);
    line->addWidget(m_command, 1);
    line->addWidget(run);
    line->addWidget(m_summary);
    layout->addLayout(line);

    connect(m_levels, &QButtonGroup::idClicked, this, [this](int) { applyFilters(); });
    connect(m_source, &QComboBox::currentIndexChanged, this, [this](int) { applyFilters(); });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) { applyFilters(); });
    connect(clear, &QPushButton::clicked, this, [this] { m_console->clear(); });
    connect(copy, &QPushButton::clicked, this, [this] {
        QStringList lines;
        for (const AppConsole::Entry &e : m_model->rows())
            lines << QStringLiteral("%1  %2  %3  %4")
                         .arg(QDateTime::fromMSecsSinceEpoch(e.time).toString(QStringLiteral("HH:mm:ss")), AppConsole::levelName(e.level).toUpper(), e.source, e.text);
        QApplication::clipboard()->setText(lines.join('\n'));
    });
    const auto submit = [this] {
        const QString text = m_command->text();
        m_command->clear();
        runCommand(text);
    };
    connect(m_command, &QLineEdit::returnPressed, this, submit);
    connect(run, &QPushButton::clicked, this, submit);

    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &ConsolePanel::refresh);
    m_timer.start();
    refresh();
}

int ConsolePanel::shownCount() const { return m_model->rowCount(); }

void ConsolePanel::setLevelFilter(const QString &name)
{
    static const QStringList names{QStringLiteral("all"), QStringLiteral("errors"), QStringLiteral("warnings"), QStringLiteral("info")};
    const int id = names.indexOf(name.toLower());
    if (id >= 0) m_levelButtons[id]->setChecked(true);
    if (id == 0) {
        m_source->setCurrentIndex(0);
        m_search->clear();
    }
    applyFilters();
}

void ConsolePanel::say(AppConsole::Level level, const QString &text) { m_console->add(level, QStringLiteral("Console"), text); }

// The command line: for now a few commands of its own; a script will run here later
void ConsolePanel::runCommand(const QString &line)
{
    const QString text = line.simplified();
    if (text.isEmpty()) return;
    say(AppConsole::Info, QStringLiteral("› ") + text);
    const QStringList words = text.split(' ');
    const QString command = words.first().toLower();
    if (command == QLatin1String("clear")) {
        m_console->clear();
    } else if (command == QLatin1String("filter")) {
        const QString what = words.value(1, QStringLiteral("all"));
        static const QStringList known{QStringLiteral("all"), QStringLiteral("errors"), QStringLiteral("warnings"), QStringLiteral("info")};
        if (known.contains(what.toLower())) setLevelFilter(what);
        else say(AppConsole::Error, QStringLiteral("filter: all, errors, warnings or info"));
    } else if (command == QLatin1String("help")) {
        say(AppConsole::Info, QStringLiteral("help — this list · clear — forget every message · filter all|errors|warnings|info — what is shown. "
                                             "Scripts to drive NyxMap will run here later."));
    } else {
        say(AppConsole::Error, QStringLiteral("Unknown command \"%1\". Try help. (Scripts are not available yet.)").arg(words.first()));
    }
}

void ConsolePanel::refresh()
{
    const quint64 g = m_console->generation();
    if (g == m_seen) return;
    m_seen = g;
    applyFilters();
}

void ConsolePanel::applyFilters()
{
    const std::vector<AppConsole::Entry> all = m_console->entries();
    // The sources there are, kept as chosen
    const QString chosen = m_source->currentIndex() > 0 ? m_source->currentText() : QString();
    QStringList sources;
    for (const AppConsole::Entry &e : all)
        if (!sources.contains(e.source)) sources << e.source;
    sources.sort();
    {
        const QSignalBlocker block(m_source);
        m_source->clear();
        m_source->addItem(QStringLiteral("Source: all"));
        m_source->addItems(sources);
        const int at = chosen.isEmpty() ? 0 : m_source->findText(chosen);
        m_source->setCurrentIndex(std::max(0, at));
    }
    const int level = m_levels->checkedId(); // 0 all, 1 errors, 2 warnings, 3 info
    const QString source = m_source->currentIndex() > 0 ? m_source->currentText() : QString();
    const QString text = m_search->text().trimmed();
    int counts[3] = {};
    std::vector<AppConsole::Entry> rows;
    for (const AppConsole::Entry &e : all) {
        ++counts[int(e.level)];
        if (level == 1 && e.level != AppConsole::Error) continue;
        if (level == 2 && e.level != AppConsole::Warning) continue;
        if (level == 3 && e.level != AppConsole::Info) continue;
        if (!source.isEmpty() && e.source != source) continue;
        if (!text.isEmpty() && !e.text.contains(text, Qt::CaseInsensitive)) continue;
        rows.push_back(e);
    }
    QScrollBar *bar = m_view->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum() - 2; // following the end
    m_model->setRows(std::move(rows));
    if (atEnd) m_view->scrollToBottom();
    // The counts on the buttons: how many of each there are
    m_levelButtons[0]->setText(QStringLiteral("All %1").arg(int(all.size())));
    m_levelButtons[1]->setText(QStringLiteral("Errors %1").arg(counts[AppConsole::Error]));
    m_levelButtons[2]->setText(QStringLiteral("Warnings %1").arg(counts[AppConsole::Warning]));
    m_levelButtons[3]->setText(QStringLiteral("Info %1").arg(counts[AppConsole::Info]));
    m_summary->setText(m_model->rowCount() == int(all.size()) ? QString() : QStringLiteral("%1 of %2").arg(m_model->rowCount()).arg(int(all.size())));
}
