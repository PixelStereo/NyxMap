#include "ShowControlPanel.h"
#include "Engine.h"
#include "ShowDesk.h"
#include "Widgets.h"

#include <QComboBox>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
const char *const kGreen = "#4cd964";
const char *const kRed = "#e84c3d";
const char *const kAmber = "#e0b43a";
const char *const kBlue = "#569ce6";

QString phaseColor(const QString &phase)
{
    return phase == QLatin1String("pre-wait") ? kAmber : phase == QLatin1String("post-wait") ? kBlue : kGreen;
}

QString modeColor(const QString &mode)
{
    if (mode == QLatin1String("wait")) return QStringLiteral("#d5bd82");
    if (mode == QLatin1String("follow")) return QStringLiteral("#79cbb6");
    if (mode == QLatin1String("auto-follow")) return QStringLiteral("#b7a1d9");
    return QStringLiteral("#a4a4ae");
}

QString clockText(double seconds)
{
    if (seconds < 0 || !std::isfinite(seconds)) return QStringLiteral("∞");
    const qint64 tenths = std::llround(seconds * 10);
    return QStringLiteral("%1:%2.%3").arg(tenths / 600).arg((tenths / 10) % 60, 2, 10, QLatin1Char('0')).arg(tenths % 10);
}

QStringList continuation(const QString &mode, double postwait)
{
    if (mode == QLatin1String("follow")) return {QStringLiteral("FOLLOW"), QStringLiteral("Start + ") + clockText(postwait)};
    if (mode == QLatin1String("auto-follow")) return {QStringLiteral("AUTO-FOLLOW"), QStringLiteral("End + ") + clockText(postwait)};
    if (mode == QLatin1String("wait")) return {QStringLiteral("WAIT · GO"), QStringLiteral("Manual advance")};
    return {QStringLiteral("—"), QString()};
}

QPushButton *deskButton(const QString &text, const QString &color, int height = 48)
{
    auto *b = new QPushButton(text);
    b->setMinimumHeight(height);
    b->setFocusPolicy(Qt::NoFocus); // Space stays GO, as everywhere in the window
    b->setStyleSheet(QStringLiteral("QPushButton { border:1px solid %1; border-radius:5px; background:transparent; font-weight:600; padding:4px 8px; color:#ececf0; }"
                                    "QPushButton:pressed { background:%1; color:#16161a; }"
                                    "QPushButton:disabled { border-color:#44444c; color:#66666e; }").arg(color));
    return b;
}

QLabel *small(const QString &text, const QString &css)
{
    auto *l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("QLabel { background:transparent; %1 }").arg(css));
    l->setWordWrap(true);
    return l;
}

QScrollArea *scrolled(QWidget *page)
{
    auto *area = new QScrollArea;
    area->setWidget(page);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return area;
}

template <class Layout> void clearLayout(Layout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}
} // namespace

// A cue's card: its tint, its edge, and how far it is drawn behind its text
class CueFill : public QWidget
{
public:
    CueFill(const QString &edge, const QString &tint) : m_edge(edge), m_tint(tint) { setObjectName(QStringLiteral("cueCard")); }
    void set(double progress, const QColor &color)
    {
        progress = std::clamp(progress, 0.0, 1.0);
        if (progress == m_progress && color == m_color) return;
        m_progress = progress;
        m_color = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), m_tint);
        QColor fill = m_color;
        fill.setAlphaF(0.18);
        p.fillRect(QRectF(0, 0, width() * m_progress, height()), fill);
        p.fillRect(QRect(0, 0, 3, height()), m_edge);
    }

private:
    QColor m_edge, m_tint, m_color = kGreen;
    double m_progress = 0;
};

QWidget *ShowControlPanel::makeCard(const QString &role, const QString &heading, const QString &edge, const QString &tint)
{
    auto *card = new CueFill(edge, tint);
    card->setObjectName(QStringLiteral("cue_") + role);
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(12, 8, 8, 8);
    v->setSpacing(2);
    const bool current = role == QLatin1String("current"), previous = role == QLatin1String("previous");
    Card c;
    c.fill = card;
    v->addWidget(small(heading, QStringLiteral("font-size:11px; font-weight:700; letter-spacing:2px; color:%1;").arg(edge)));
    c.continuation = small({}, QStringLiteral("font-size:12px; font-weight:700;"));
    v->addWidget(c.continuation);
    c.label = small({}, QStringLiteral("font-size:%1px; font-weight:600; color:%2;").arg(current ? 24 : 17).arg(previous ? QStringLiteral("#a4a4ae") : QStringLiteral("#ececf0")));
    c.label->setObjectName(QStringLiteral("cueLabel_") + role);
    v->addWidget(c.label);
    c.text = small({}, QStringLiteral("font-size:%1px; color:%2;").arg(current ? 17 : 14).arg(previous ? QStringLiteral("#9797a0") : QStringLiteral("#d2d2d9")));
    v->addWidget(c.text);
    if (!previous) {
        c.description = small({}, QStringLiteral("font-size:12px; color:#a4a4ae;"));
        v->addWidget(c.description);
        c.phase = small({}, QStringLiteral("font-size:12px; font-weight:600;"));
        v->addWidget(c.phase);
        c.times = small({}, QStringLiteral("font-size:11px; font-family:monospace; color:#a4a4ae;"));
        v->addWidget(c.times);
    }
    m_cards.insert(role, c);
    return card;
}

ShowControlPanel::ShowControlPanel(Engine *engine, QWidget *parent) : QWidget(parent), m_e(engine)
{
    setObjectName(QStringLiteral("showControlPanel"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto *tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("showControlTabs"));
    tabs->setStyleSheet(theme::tabStyle());
    tabs->setDocumentMode(true);
    root->addWidget(tabs);

    // SHOW
    auto *show = new QWidget;
    auto *v = new QVBoxLayout(show);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(8);
    auto *toLayers = deskButton(QStringLiteral("LAYER CONTROL →"), QStringLiteral("#79cbb6"), 36);
    connect(toLayers, &QPushButton::clicked, this, &ShowControlPanel::layersRequested);
    v->addWidget(toLayers);
    m_sequence = new QComboBox;
    m_sequence->setObjectName(QStringLiteral("showSequence"));
    connect(m_sequence, qOverload<int>(&QComboBox::activated), this, [this](int) {
        command({{"c", "select"}, {"id", m_sequence->currentData().toString()}});
    });
    v->addWidget(m_sequence);
    m_running = small({}, QStringLiteral("font-size:12px; color:#b2b2bc;"));
    v->addWidget(m_running);
    v->addWidget(makeCard(QStringLiteral("previous"), QStringLiteral("PREVIOUS"), QStringLiteral("#676771"), QStringLiteral("#242428")));
    v->addWidget(makeCard(QStringLiteral("current"), QStringLiteral("CURRENT"), kGreen, QStringLiteral("#262b28")));
    v->addWidget(makeCard(QStringLiteral("next"), QStringLiteral("NEXT"), kRed, QStringLiteral("#2c2728")));

    auto *transport = new QGridLayout;
    transport->setSpacing(6);
    m_back = deskButton(QStringLiteral("‹ GO BACK"), QStringLiteral("#94949f"), 56);
    m_back->setObjectName(QStringLiteral("showBack"));
    m_go = deskButton(QStringLiteral("GO ▶"), kGreen, 56);
    m_go->setObjectName(QStringLiteral("showGo"));
    QFont big = m_go->font();
    big.setPixelSize(24);
    m_go->setFont(big);
    m_stop = deskButton(QStringLiteral("STOP WAITS"), kRed);
    m_stop->setObjectName(QStringLiteral("showStop"));
    m_stopAll = deskButton(QStringLiteral("STOP ALL WAITS"), kRed);
    m_stopAll->setObjectName(QStringLiteral("showStopAll"));
    m_stopAll->setToolTip(QStringLiteral("Double-click: every sequence"));
    transport->addWidget(m_back, 0, 0);
    transport->addWidget(m_go, 0, 1);
    transport->setColumnStretch(0, 1);
    transport->setColumnStretch(1, 2);
    transport->addWidget(m_stop, 1, 0);
    transport->addWidget(m_stopAll, 1, 1);
    v->addLayout(transport);
    const auto selected = [this] { return QString::number(m_selected); };
    connect(m_go, &QPushButton::clicked, this, [this, selected] { command({{"c", "go"}, {"id", selected()}}); });
    connect(m_back, &QPushButton::clicked, this, [this, selected] { command({{"c", "back"}, {"id", selected()}}); });
    connect(m_stop, &QPushButton::clicked, this, [this, selected] { command({{"c", "stop"}, {"id", selected()}}); });
    connect(m_stopAll, &QPushButton::clicked, this, [this] { // twice: it stops every sequence
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_stopAllArmed < 450) {
            m_stopAllArmed = 0;
            command({{"c", "stop_all"}});
        } else {
            m_stopAllArmed = now;
        }
    });

    auto clockBox = [&](const QString &title, const QString &color, QLabel *&value) {
        auto *box = new QWidget;
        box->setObjectName(QStringLiteral("showClock"));
        box->setStyleSheet(QStringLiteral("#showClock { background:#26262b; border-radius:5px; }"));
        auto *b = new QVBoxLayout(box);
        b->setContentsMargins(8, 6, 8, 6);
        b->setSpacing(0);
        b->addWidget(small(title, QStringLiteral("font-size:11px; letter-spacing:2px; color:#a4a4ae;")));
        value = small(QStringLiteral("0:00.0"), QStringLiteral("font-size:30px; font-family:monospace; color:%1;").arg(color));
        value->setAlignment(Qt::AlignCenter);
        value->setWordWrap(false);
        b->addWidget(value);
        return box;
    };
    auto *clocks = new QHBoxLayout;
    clocks->setSpacing(6);
    clocks->addWidget(clockBox(QStringLiteral("ELAPSED"), QStringLiteral("#ececf0"), m_elapsed));
    clocks->addWidget(clockBox(QStringLiteral("REMAINING"), kAmber, m_remaining));
    v->addLayout(clocks);
    m_elapsed->setObjectName(QStringLiteral("showElapsed"));
    m_remaining->setObjectName(QStringLiteral("showRemaining"));
    m_phase = new QProgressBar;
    m_phase->setObjectName(QStringLiteral("showPhase"));
    m_phase->setRange(0, 1000);
    m_phase->setTextVisible(false);
    m_phase->setFixedHeight(14);
    v->addWidget(m_phase);
    v->addWidget(small(QStringLiteral("STOP WAITS cancels pending cues and follows. It does not stop media or timelines. Double-click STOP ALL WAITS for every sequence."),
                       QStringLiteral("font-size:11px; color:#9999a4;")));
    v->addWidget(small(QStringLiteral("CUE STACK"), QStringLiteral("font-size:11px; font-weight:600; letter-spacing:2px; color:#a4a4ae;")));
    m_cues = new QVBoxLayout;
    m_cues->setSpacing(6);
    v->addLayout(m_cues);
    v->addStretch();
    tabs->addTab(scrolled(show), QStringLiteral("SHOW"));

    // The libraries
    auto library = [&](const QString &title, QGridLayout *&grid) {
        auto *page = new QWidget;
        auto *p = new QVBoxLayout(page);
        p->setContentsMargins(8, 8, 8, 8);
        grid = new QGridLayout;
        grid->setSpacing(6);
        p->addLayout(grid);
        p->addStretch();
        tabs->addTab(scrolled(page), title);
    };
    library(QStringLiteral("SNAPSHOTS"), m_snapshots);
    library(QStringLiteral("PRESETS"), m_presets);
    auto *timelines = new QWidget;
    auto *t = new QVBoxLayout(timelines);
    t->setContentsMargins(8, 8, 8, 8);
    m_timelineList = new QVBoxLayout;
    m_timelineList->setSpacing(8);
    t->addLayout(m_timelineList);
    t->addStretch();
    tabs->addTab(scrolled(timelines), QStringLiteral("TIMELINES"));

    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &ShowControlPanel::refresh);
}

void ShowControlPanel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    m_tick = 0;
    refresh();
    m_timer->start();
}

void ShowControlPanel::hideEvent(QHideEvent *e)
{
    QWidget::hideEvent(e);
    m_timer->stop();
}

void ShowControlPanel::command(const QJsonObject &o)
{
    showdesk::command(m_e, o);
    refresh();
}

void ShowControlPanel::renderCard(const QString &role, const QJsonObject &values)
{
    const Card c = m_cards.value(role);
    const auto at = [&](const char *name) { return values.value(QStringLiteral("/sequence/cue/%1/%2").arg(role, QLatin1String(name))); };
    const bool any = at("number").toInt() > 0;
    const QString phase = at("phase").toString(), mode = at("continue").toString();
    c.fill->set(at("progress").toDouble(), QColor(phaseColor(phase)));
    const QStringList m = continuation(mode, at("postwait").toDouble());
    c.continuation->setText(any ? m[0] + (m[1].isEmpty() ? QString() : QStringLiteral(" · ") + m[1]) : QString());
    c.continuation->setStyleSheet(QStringLiteral("QLabel { background:transparent; font-size:12px; font-weight:700; color:%1; }").arg(modeColor(mode)));
    c.label->setText(any ? at("label").toString() : QStringLiteral("—"));
    c.text->setText(at("text").toString());
    if (c.description) c.description->setText(at("description").toString());
    if (c.phase) {
        c.phase->setText(phase.toUpper());
        c.phase->setStyleSheet(QStringLiteral("QLabel { background:transparent; font-size:12px; font-weight:600; color:%1; }")
                                   .arg(phase.isEmpty() || phase == QLatin1String("done") ? QStringLiteral("#9797a0") : phaseColor(phase)));
    }
    if (c.times) {
        const double action = at("action").toDouble();
        c.times->setText(any ? QStringLiteral("Pre %1 s · Action %2 · Post %3 s")
                                   .arg(at("prewait").toDouble(), 0, 'f', 1)
                                   .arg(action < 0 ? QStringLiteral("∞") : QStringLiteral("%1 s").arg(action, 0, 'f', 1))
                                   .arg(at("postwait").toDouble(), 0, 'f', 1)
                             : QString());
    }
}

void ShowControlPanel::refresh()
{
    if (m_tick++ % 5 == 0) { // the project's lists: twice a second is enough to follow an edit
        const QJsonObject catalog = showdesk::catalog(m_e);
        if (catalog != m_catalog) {
            m_catalog = catalog;
            rebuildLists();
        }
    }
    const QJsonObject state = showdesk::state(m_e), values = state.value("values").toObject();
    const quint64 selected = values.value("/sequence/id").toString().toULongLong();
    if (selected != m_selected) {
        m_selected = selected;
        rebuildCues();
    }
    const int index = m_sequence->findData(QString::number(selected));
    if (index != m_sequence->currentIndex()) m_sequence->setCurrentIndex(index);
    int background = 0;
    for (const QJsonValue &s : state.value("sequences").toArray()) {
        const QJsonObject o = s.toObject();
        background += o.value("active").toBool() && o.value("id").toString().toULongLong() != selected;
    }
    m_running->setText(QStringLiteral("%1 · %2 background")
                           .arg(values.value("/sequence/running").toInt() ? QStringLiteral("WAITS / FOLLOW ACTIVE") : QStringLiteral("WAITING FOR GO"))
                           .arg(background));
    for (const QString &role : {QStringLiteral("previous"), QStringLiteral("current"), QStringLiteral("next")}) renderCard(role, values);
    m_elapsed->setText(values.value("/sequence/cue/current/elapsed_text").toString(QStringLiteral("0:00.0")));
    m_remaining->setText(values.value("/sequence/cue/current/remaining_text").toString(QStringLiteral("0:00.0")));
    m_phase->setValue(int(std::clamp(values.value("/sequence/cue/current/phase_progress").toDouble(), 0.0, 1.0) * 1000));
    m_phase->setStyleSheet(QStringLiteral("QProgressBar { background:#2c2c32; border:0; border-radius:3px; } QProgressBar::chunk { background:%1; border-radius:3px; }")
                               .arg(phaseColor(values.value("/sequence/cue/current/phase").toString())));
    m_go->setEnabled(values.value("/sequence/go_enabled").toInt());
    m_back->setEnabled(values.value("/sequence/back_enabled").toInt());
    m_stop->setEnabled(m_selected);
    const int current = values.value("/sequence/cue/current/number").toInt(), next = values.value("/sequence/cue/next/number").toInt();
    if (current != m_currentCue || next != m_nextCue) {
        m_currentCue = current;
        m_nextCue = next;
        markCues();
    }
    for (const QJsonValue &value : state.value("timelines").toArray()) {
        const QJsonObject o = value.toObject();
        const auto row = m_timelines.constFind(o.value("id").toString());
        if (row == m_timelines.constEnd()) continue;
        row->progress->setValue(int(std::clamp(o.value("progress").toDouble(), 0.0, 1.0) * 1000));
        row->clock->setText(QStringLiteral("%1 · %2 / %3 · Pass %4 · Left %5")
                                .arg(o.value("state").toString().toUpper(), clockText(o.value("position").toDouble()),
                                     clockText(o.value("duration").toDouble()))
                                .arg(o.value("pass").toInt())
                                .arg(clockText(o.value("remaining").toDouble())));
    }
}

void ShowControlPanel::rebuildLists()
{
    m_sequence->clear();
    for (const QJsonValue &value : m_catalog.value("sequences").toArray()) {
        const QJsonObject o = value.toObject();
        m_sequence->addItem(QStringLiteral("%1 · %2").arg(o.value("index").toInt()).arg(o.value("name").toString(QStringLiteral("Untitled"))), o.value("id").toString());
    }
    rebuildCues();
    const auto library = [this](QGridLayout *grid, const char *list, const char *kind, const QString &color, const QString &none) {
        clearLayout(grid);
        const QJsonArray entries = m_catalog.value(QLatin1String(list)).toArray();
        if (entries.isEmpty()) grid->addWidget(small(none, QStringLiteral("color:#a4a4ae;")), 0, 0);
        int k = 0;
        for (const QJsonValue &value : entries) {
            const QJsonObject o = value.toObject();
            auto *b = deskButton(QStringLiteral("%1 · %2").arg(o.value("index").toInt()).arg(o.value("name").toString(QStringLiteral("Untitled"))), color, 64);
            b->setObjectName(QStringLiteral("%1_%2").arg(QLatin1String(kind)).arg(o.value("index").toInt()));
            connect(b, &QPushButton::clicked, this, [this, kind, id = o.value("id").toString()] { command({{"c", QLatin1String(kind)}, {"id", id}}); });
            grid->addWidget(b, k / 2, k % 2);
            ++k;
        }
    };
    library(m_snapshots, "snapshots", "snapshot", kGreen, QStringLiteral("No snapshots in this project."));
    library(m_presets, "presets", "preset", kAmber, QStringLiteral("No presets in this project."));
    clearLayout(m_timelineList);
    m_timelines.clear();
    const QJsonArray timelines = m_catalog.value("timelines").toArray();
    if (timelines.isEmpty()) m_timelineList->addWidget(small(QStringLiteral("No timelines in this project."), QStringLiteral("color:#a4a4ae;")));
    for (const QJsonValue &value : timelines) {
        const QJsonObject o = value.toObject();
        const QString id = o.value("id").toString();
        auto *card = new QWidget;
        card->setObjectName(QStringLiteral("showTimeline"));
        card->setStyleSheet(QStringLiteral("#showTimeline { background:#26262b; border-radius:5px; }"));
        auto *c = new QVBoxLayout(card);
        c->setContentsMargins(8, 8, 8, 8);
        c->addWidget(small(QStringLiteral("%1 · %2").arg(o.value("index").toInt()).arg(o.value("name").toString(QStringLiteral("Untitled"))), QStringLiteral("font-size:15px; font-weight:600;")));
        TimelineRow row;
        row.clock = small(QStringLiteral("Stopped"), QStringLiteral("font-size:11px; font-family:monospace; color:#a4a4ae;"));
        c->addWidget(row.clock);
        row.progress = new QProgressBar;
        row.progress->setRange(0, 1000);
        row.progress->setTextVisible(false);
        row.progress->setFixedHeight(10);
        row.progress->setStyleSheet(QStringLiteral("QProgressBar { background:#2c2c32; border:0; border-radius:3px; } QProgressBar::chunk { background:#56c4ff; border-radius:3px; }"));
        c->addWidget(row.progress);
        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(4);
        for (const char *action : {"play", "pause", "stop"}) {
            auto *b = deskButton(QString::fromLatin1(action).toUpper(), qstrcmp(action, "stop") ? QStringLiteral("#56c4ff") : QString::fromLatin1(kRed), 36);
            connect(b, &QPushButton::clicked, this, [this, id, action] { command({{"c", "timeline"}, {"id", id}, {"do", QLatin1String(action)}}); });
            buttons->addWidget(b);
        }
        c->addLayout(buttons);
        m_timelineList->addWidget(card);
        m_timelines.insert(id, row);
    }
}

void ShowControlPanel::rebuildCues()
{
    clearLayout(m_cues);
    m_cueButtons.clear();
    m_currentCue = m_nextCue = -1;
    const QString selected = QString::number(m_selected);
    QJsonArray cues;
    for (const QJsonValue &value : m_catalog.value("sequences").toArray())
        if (value.toObject().value("id").toString() == selected) cues = value.toObject().value("cues").toArray();
    if (cues.isEmpty()) m_cues->addWidget(small(QStringLiteral("Select a sequence containing cues."), QStringLiteral("color:#a4a4ae;")));
    for (const QJsonValue &value : cues) {
        const QJsonObject o = value.toObject();
        const int number = o.value("index").toInt();
        QString name = o.value("text").toString();
        if (name.isEmpty()) name = o.value("name").toString();
        if (name.isEmpty()) name = o.value("missing").toBool() ? QStringLiteral("Missing target") : QStringLiteral("Empty cue");
        QStringList details;
        for (const char *key : {"target_type", "name", "description"})
            if (const QString part = o.value(QLatin1String(key)).toString(); !part.isEmpty()) details << part;
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        auto *b = deskButton(QStringLiteral("%1 · %2").arg(number).arg(name), QStringLiteral("#6d6d78"), 52);
        b->setObjectName(QStringLiteral("cue_%1").arg(number));
        b->setToolTip(details.join(QStringLiteral(" · ")));
        connect(b, &QPushButton::clicked, this, [this, selected, id = o.value("id").toString()] { command({{"c", "cue"}, {"id", selected}, {"cue", id}}); });
        row->addWidget(b, 1);
        const QString mode = o.value("continue").toString();
        const QStringList m = continuation(mode, o.value("postwait").toDouble());
        auto *badge = small(m[0] + (m[1].isEmpty() ? QString() : QStringLiteral("\n") + m[1]),
                            QStringLiteral("font-size:11px; font-weight:600; color:%1; border:1px solid %1; border-radius:4px; padding:3px;").arg(modeColor(mode)));
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedWidth(104);
        row->addWidget(badge);
        auto *holder = new QWidget;
        holder->setLayout(row);
        row->setContentsMargins(0, 0, 0, 0);
        m_cues->addWidget(holder);
        m_cueButtons.insert(number, b);
    }
}

void ShowControlPanel::markCues()
{
    for (auto it = m_cueButtons.constBegin(); it != m_cueButtons.constEnd(); ++it) {
        const QString color = it.key() == m_currentCue ? kGreen : it.key() == m_nextCue ? kRed : "#6d6d78";
        it.value()->setStyleSheet(QStringLiteral("QPushButton { border:1px solid %1; border-radius:5px; background:transparent; font-weight:600; padding:4px 8px; color:#ececf0; text-align:left; }"
                                                 "QPushButton:pressed { background:%1; color:#16161a; }").arg(color));
    }
}
