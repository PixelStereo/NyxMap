#include "SequencePanel.h"
#include "Engine.h"
#include "SnapshotPanel.h"
#include "TimelinePanel.h"
#include "Widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QMap>
#include <QPushButton>
#include <QSet>
#include "LibraryTree.h"
#include <QFrame>
#include <QGridLayout>
#include <QKeyEvent>
#include <QScreen>
#include <QCursor>
#include <QAbstractItemView>
#include <QShortcut>
#include <set>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>

static QString snapshotName(Engine *e, quint64 id)
{
    if (!id) return QStringLiteral("—");
    const int i = e->indexOfSnapshot(id);
    if (i < 0) return QStringLiteral("(snapshot gone)");
    const Engine::Snapshot m = e->snapshot(i);
    return QStringLiteral("%1  %2").arg(i + 1).arg(m.name);
}

// What a timeline step does
static QString actionName(const Sequencer::Step &st)
{
    using A = Engine::AnimAction;
    switch (st.action) {
    case A::Play: return QStringLiteral("▶ Play");
    case A::Pause: return QStringLiteral("❚❚ Pause");
    case A::Stop: return QStringLiteral("■ Stop");
    case A::Rewind: return QStringLiteral("⏮ Rewind");
    case A::Seek: return QStringLiteral("Seek %1 s").arg(st.seekTime, 0, 'f', 2);
    case A::Speed: return QStringLiteral("Speed × %1").arg(st.speed, 0, 'f', 2);
    case A::LoopMode: {
        const QString n = st.repeat > 0 ? QStringLiteral(" × %1").arg(st.repeat) : QStringLiteral(" ∞");
        return st.loop == Engine::AnimLoop::Once ? QStringLiteral("Loop: once")
             : st.loop == Engine::AnimLoop::PingPong ? QStringLiteral("Loop: ping-pong") + n
                                                    : QStringLiteral("Loop") + n;
    }
    }
    return {};
}

static QString timelineName(Engine *e, quint64 id)
{
    const int i = e->indexOfAnimation(id);
    return i < 0 ? QStringLiteral("(timeline gone)") : e->animation(i).name;
}

// What a step does: its snapshot (with its number), or its timeline and the action on it
static QString stepTarget(Engine *e, const Sequencer::Step &st, bool numbered = true)
{
    if (!st.actions.empty()) {
        QStringList names;
        for (const auto &action : st.actions)
            names << stepTarget(e, action, false);
        return QStringLiteral("Group · %1 steps: %2").arg(st.actions.size()).arg(names.join(QStringLiteral(" + ")));
    }
    if (st.sequence) {
        const int index = e->sequencer().indexOfSequence(st.sequence);
        return index < 0 ? QStringLiteral("(sequence gone)") : QStringLiteral("▶ Sequence · %1").arg(e->sequencer().sequence(index).name);
    }
    if (st.preset) {
        const int index = e->indexOfPreset(st.preset);
        return index < 0 ? QStringLiteral("(preset gone)") : QStringLiteral("Preset · %1").arg(e->preset(index).name);
    }
    if (st.timeline) return QStringLiteral("≋ %1 · %2").arg(timelineName(e, st.timeline), actionName(st));
    if (numbered) return snapshotName(e, st.snapshot);
    const int mi = e->indexOfSnapshot(st.snapshot);
    return !st.snapshot ? QStringLiteral("—") : mi < 0 ? QStringLiteral("(snapshot gone)") : e->snapshot(mi).name;
}

static bool targetGone(Engine *e, const Sequencer::Step &st)
{
    if (st.sequence) return e->sequencer().indexOfSequence(st.sequence) < 0;
    if (st.preset) return e->indexOfPreset(st.preset) < 0;
    return st.timeline ? e->indexOfAnimation(st.timeline) < 0 : st.snapshot && e->indexOfSnapshot(st.snapshot) < 0;
}

static const char *kChanged = "#ffb347"; // something changed since the step was played
static const QColor kLive(76, 217, 100);  // a snapshot running
static const QColor kPreWait(224, 180, 58);  // waiting before the snapshot
static const QColor kPostWait(86, 156, 230); // waiting before the next step's GO

// Seconds as the cue list shows them: 2.5 · 1:05.0
// A step's timecode as shown: minutes, seconds and hundredths ("1:02.35"); none: empty
static QString timecodeText(double t)
{
    if (t < 0) return QString();
    const int m = int(t / 60);
    return QStringLiteral("%1:%2").arg(m).arg(t - m * 60, 5, 'f', 2, QLatin1Char('0'));
}

// A timecode as typed: seconds ("12.35"), minutes:seconds ("1:02.35"), hours:minutes:seconds; empty: none (-1)
static double parseTimecode(const QString &text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (text.trimmed().isEmpty() || parts.size() > 3) return -1;
    double t = 0;
    for (const QString &part : parts) {
        bool ok = false;
        const double v = QString(part).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
        if (!ok || v < 0) return -1;
        t = t * 60 + v;
    }
    return t;
}

static QString seconds(double t)
{
    if (!std::isfinite(t)) return QStringLiteral("∞");
    t = std::max(0.0, t);
    if (t < 60) return QString::number(t, 'f', 1);
    const int m = int(t / 60);
    return QStringLiteral("%1:%2").arg(m).arg(t - m * 60, 4, 'f', 1, QLatin1Char('0'));
}

static QString continueName(Sequencer::StepContinue c)
{
    switch (c) {
    case Sequencer::StepContinue::AutoFollow: return QStringLiteral("AUTO");
    default: return QStringLiteral("GO");
    }
}

static QColor continueColor(Sequencer::StepContinue c)
{
    switch (c) {
    case Sequencer::StepContinue::AutoFollow: return QColor("#b7a1d9");
    default: return QColor("#d5bd82");
    }
}

static QString continueHelp(Sequencer::StepContinue c)
{
    switch (c) {
    case Sequencer::StepContinue::AutoFollow: return QStringLiteral("AUTO: next step goes when this action ends + post-wait; an endless action holds here. Double-click: wait for a GO.");
    default: return QStringLiteral("GO: next step waits for a manual GO. Post-wait is not used. Double-click: go on by itself.");
    }
}

static void drawContinueBadge(QPainter *p, const QRectF &rect, Sequencer::StepContinue mode, QFont font)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QColor ink = continueColor(mode);
    QColor fill = ink;
    fill.setAlphaF(.12);
    QColor edge = ink;
    edge.setAlphaF(.6);
    p->setPen(QPen(edge, 1));
    p->setBrush(fill);
    p->drawRoundedRect(rect, 3, 3);
    font.setBold(true);
    p->setFont(font);
    p->setPen(ink);
    p->drawText(rect, Qt::AlignCenter, continueName(mode));
    p->restore();
}

// Where a step on its way is in each of its times: -1 not there yet (or none), 0..1 on the way, 1 over
struct RunPhases {
    double pre = -1, action = -1, post = -1;
    double preElapsed = 0, actionElapsed = 0, postElapsed = 0;
};
static RunPhases phasesOf(const Sequencer::Run &r)
{
    RunPhases ph;
    auto frac = [](double e, double len) { return len > 0 ? std::clamp(e / len, 0.0, 1.0) : (e >= 0 ? 1.0 : 0.0); };
    ph.preElapsed = std::min(r.elapsed, r.preWait);
    ph.pre = r.preWait > 0 ? frac(r.elapsed, r.preWait) : -1;
    if (r.fired) {
        ph.actionElapsed = std::clamp(r.elapsed - r.preWait, 0.0, r.duration);
        ph.action = frac(r.elapsed - r.preWait, r.duration);
    }
    const double at = r.continueAt();
    if (at >= 0 && r.fired) {
        const double start = at - r.postWait;
        if (r.elapsed >= start) {
            ph.postElapsed = std::clamp(r.elapsed - start, 0.0, r.postWait);
            ph.post = frac(r.elapsed - start, r.postWait);
        }
    }
    return ph;
}

// ---------------------------------------------------------------------------
// RecallProgressBar
// ---------------------------------------------------------------------------
RecallProgressBar::RecallProgressBar(Engine *engine, QWidget *parent) : QWidget(parent), m_engine(engine)
{
    setFixedHeight(5);
    auto *t = new QTimer(this);
    t->setInterval(33);
    connect(t, &QTimer::timeout, this, &RecallProgressBar::poll);
    t->start();
}

void RecallProgressBar::poll()
{
    const Engine::RecallProgress r = m_engine->recallProgress();
    const double f = r.snapshot ? r.fraction() : 0.0;
    const bool running = r.running();
    if (std::abs(f - m_fraction) < 1e-4 && running == m_running) return;
    m_fraction = f;
    m_running = running;
    const int mi = m_engine->indexOfSnapshot(r.snapshot);
    setToolTip(mi < 0 ? QString()
                      : QStringLiteral("%1. %2 — %3 / %4 s").arg(mi + 1).arg(m_engine->snapshot(mi).name)
                            .arg(std::min(r.elapsed, r.total), 0, 'f', 1).arg(r.total, 0, 'f', 1));
    update();
}

void RecallProgressBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(40, 40, 44));
    if (m_fraction <= 0) return;
    QColor c = m_running ? kLive : QColor(kLive.red(), kLive.green(), kLive.blue(), 90); // done: dimmed
    p.fillRect(QRectF(0, 0, width() * m_fraction, height()), c);
}

static const QColor kUpcoming(232, 76, 61); // the step GO plays: its frame

// ---------------------------------------------------------------------------
// SequenceWindow
// ---------------------------------------------------------------------------

// The steps of a group played one GO at a time, the one at `step` being of it
static void folderRange(const Sequencer::Sequence &s, int step, int *first, int *last)
{
    const quint64 folder = s.steps[size_t(step)].folder;
    *first = *last = step;
    while (*first > 0 && s.steps[size_t(*first - 1)].folder == folder) --*first;
    while (*last + 1 < int(s.steps.size()) && s.steps[size_t(*last + 1)].folder == folder) ++*last;
}
static quint64 newFolderId(const Sequencer::Sequence &s)
{
    quint64 id = 0;
    for (const Sequencer::Step &st : s.steps) id = std::max(id, st.folder);
    for (quint64 closed : s.closedFolders) id = std::max(id, closed);
    return id + 1;
}
// Those split by a move are two groups; the fold of those gone is forgotten
static void normalizeFolders(Sequencer::Sequence &s)
{
    std::set<quint64> seen;
    quint64 previous = 0, renamed = 0, next = newFolderId(s);
    for (Sequencer::Step &st : s.steps) {
        const quint64 was = st.folder;
        if (was && was == previous) st.folder = renamed;
        else if (was && seen.count(was)) st.folder = renamed = next++;
        else renamed = was;
        previous = was;
        if (was) seen.insert(was);
    }
    std::vector<quint64> closed;
    for (quint64 id : s.closedFolders)
        if (seen.count(id)) closed.push_back(id);
    s.closedFolders = closed;
}

constexpr Qt::ItemFlags kRowFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
enum StepCol { ColNum, ColSnapshot, ColPre, ColAction, ColPost, ColQuantize, ColNext, ColDescription, ColAt, ColTime, ColCount };
constexpr int kStepOutlineRole = Qt::UserRole + 1;

static void setStepOutline(QTableWidgetItem *item, const QString &kind, const QColor &color)
{
    item->setData(kStepOutlineRole, color);
    const QString description = item->toolTip().isEmpty() ? item->text().trimmed() : item->toolTip();
    item->setToolTip(QStringLiteral("%1 · %2").arg(kind, description));
    item->setData(Qt::AccessibleTextRole, item->toolTip());
}


// The time columns (pre-wait, action, post-wait) filling as their time goes by, the continuation as a word;
// the waits are edited with the app's number field, the continuation with a menu
class StepDelegate : public QStyledItemDelegate
{
public:
    StepDelegate(SequenceWindow *w) : QStyledItemDelegate(w), m_w(w) {}

    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &idx) const override
    {
        QStyledItemDelegate::initStyleOption(option, idx);
        if (idx.column() == ColQuantize && idx.data(Qt::EditRole).isValid()) { // a grid in beats, or none
            const double beats = idx.data(Qt::EditRole).toDouble();
            option->text = beats > 0 ? QStringLiteral("%1 beat%2").arg(beats, 0, 'g', 4).arg(beats == 1 ? "" : "s") : QStringLiteral("off");
        }
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        if (idx.column() == ColQuantize) {
            auto *b = new NumberBox(parent);
            b->setRange(0, 64);
            b->setDecimals(2);
            b->setSingleStep(1);
            b->setSpecialValueText(QStringLiteral("off"));
            b->setSuffix(QStringLiteral(" beats"));
            return b;
        }
        if (idx.column() == ColAt) { // a time offered, selected: typed over, or kept
            auto *e = new QLineEdit(parent);
            e->setPlaceholderText(QStringLiteral("seconds, or m:ss.cc"));
            e->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return e;
        }
        return QStyledItemDelegate::createEditor(parent, opt, idx);
    }
    void setEditorData(QWidget *e, const QModelIndex &idx) const override
    {
        if (auto *line = qobject_cast<QLineEdit *>(e); line && idx.column() == ColAt) {
            const QString now = idx.data(Qt::DisplayRole).toString();
            line->setText(now.isEmpty() ? m_w->proposedTimecode(m_w->stepOfRow(idx.row())) : now);
            line->selectAll();
            return;
        }
        if (auto *b = qobject_cast<QDoubleSpinBox *>(e)) b->setValue(idx.data(Qt::EditRole).toDouble());
        else QStyledItemDelegate::setEditorData(e, idx);
    }
    void setModelData(QWidget *e, QAbstractItemModel *m, const QModelIndex &idx) const override
    {
        if (auto *b = qobject_cast<QDoubleSpinBox *>(e)) m->setData(idx, b->value(), Qt::EditRole);
        else QStyledItemDelegate::setModelData(e, m, idx);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        paintCell(p, opt, idx);
        if (const int role = idx.column() == ColSnapshot ? idx.data(Qt::UserRole).toInt() : 0) // a group and its steps: one line down them
            p->fillRect(QRect(opt.rect.left(), opt.rect.top(), 3, opt.rect.height()), role <= 2 ? QColor(76, 217, 100, 170) : QColor(86, 156, 230, 200));
        if (idx.column() == ColSnapshot) {
            const QColor outline = idx.data(kStepOutlineRole).value<QColor>();
            if (outline.isValid()) {
                p->save();
                p->setRenderHint(QPainter::Antialiasing);
                p->setPen(QPen(outline, 1));
                p->setBrush(Qt::NoBrush);
                p->drawRoundedRect(QRectF(opt.rect).adjusted(4.5, 3.5, -4.5, -3.5), 3, 3);
                p->restore();
            }
        }
        paintFrame(p, opt, idx);
    }

private:
    // The step played and the one GO plays: a thin frame around their whole row (each cell draws its part of it)
    void paintFrame(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const
    {
        if (m_w->actionOfRow(idx.row()) >= 0) return; // on the group's row
        const int row = idx.row();
        bool playing = row == m_w->rowOf(m_w->m_position);
        for (const Sequencer::Run &r : m_w->m_runs)
            if (row == m_w->rowOf(r.step)) playing = true;
        if (!playing && row != m_w->rowOf(m_w->m_upcoming)) return;
        const auto *view = qobject_cast<const QAbstractItemView *>(opt.widget);
        const QHeaderView *header = m_w->m_steps->horizontalHeader();
        if (!view || !header) return;
        const int first = header->logicalIndex(0), last = header->logicalIndex(header->count() - 1);
        const QRect left = view->visualRect(idx.sibling(row, first)), right = view->visualRect(idx.sibling(row, last));
        const QRectF whole = QRectF(QRect(left.topLeft(), right.bottomRight())).adjusted(1.5, 1.5, -1.5, -1.5);
        const QColor c = playing ? (m_w->m_modified ? QColor(QString::fromLatin1(kChanged)) : kLive) : kUpcoming;
        p->save();
        p->setClipRect(opt.rect);
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(c, 1));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(whole, 3, 3);
        p->restore();
    }

    void paintCell(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const
    {
        const int col = idx.column();
        if (col == ColSnapshot && idx.data(kStepOutlineRole).isValid()) {
            QStyleOptionViewItem o(opt);
            initStyleOption(&o, idx);
            const QWidget *w = opt.widget;
            auto *style = w ? w->style() : QApplication::style();
            const QString label = o.text;
            o.text.clear();
            style->drawControl(QStyle::CE_ItemViewItem, &o, p, w);
            const QRect textRect = opt.rect.adjusted(10, 0, -9, 0);
            p->save();
            p->setFont(o.font);
            const auto role = o.state.testFlag(QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text;
            p->setPen(o.palette.color(role));
            p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, o.fontMetrics.elidedText(label, Qt::ElideRight, textRect.width()));
            p->restore();
            return;
        }
        if (col != ColTime && col != ColNext) {
            QStyledItemDelegate::paint(p, opt, idx);
            return;
        }
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, idx);
        o.text.clear();
        const QWidget *w = opt.widget;
        (w ? w->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &o, p, w);
        const auto next = Sequencer::StepContinue(idx.sibling(idx.row(), ColNext).data(Qt::EditRole).toInt());
        if (col == ColNext) {
            if (m_w->actionOfRow(idx.row()) != -1) return; // the group says what comes next
            drawContinueBadge(p, QRectF(opt.rect).adjusted(7, 4, -7, -4), next, opt.font);
            return;
        }
        if (m_w->actionOfRow(idx.row()) != SequenceWindow::kHeaderRow) paintTime(p, opt, idx, next);
    }

    // The step's times on one bar, each part in its share of it: its pre-wait (yellow), its action (green), its post-wait (blue,
    // when it counts: with a follow). A part's number when it fits; the whole time at the end, or the time gone by on its way.
    void paintTime(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx, Sequencer::StepContinue next) const
    {
        const double unit = m_w->m_beats->isChecked() ? m_w->m_engine->beatClock().secondsPerBeat() : 1.0;
        const double pre = idx.sibling(idx.row(), ColPre).data(Qt::EditRole).toDouble();
        const double action = idx.sibling(idx.row(), ColAction).data(Qt::EditRole).toDouble(); // -1: endless
        const int child = m_w->actionOfRow(idx.row());
        const double post = child < 0 && next == Sequencer::StepContinue::Wait ? 0.0 : idx.sibling(idx.row(), ColPost).data(Qt::EditRole).toDouble();
        const bool endless = action < 0;
        const double known = pre * unit + std::max(0.0, action) + post * unit;
        const Sequencer::Run *run = nullptr; // on its way (the latest, should it be there twice in a loop)
        for (const Sequencer::Run &r : m_w->m_runs)
            if (r.step == m_w->stepOfRow(idx.row())) run = &r;
        RunPhases ph = run && child < 0 ? phasesOf(*run) : RunPhases();
        double gone = run ? run->elapsed : -1; // the time gone by since its GO
        if (run && child >= 0) { // a step of a group: on the group's clock, from its own pre-wait
            gone = run->fired ? run->elapsed - run->preWait : -1;
            if (child < int(run->target.actions.size()) && gone >= 0) {
                const Sequencer::Step &a = run->target.actions[size_t(child)];
                auto frac = [](double e, double len) { return len > 0 ? std::clamp(e / len, 0.0, 1.0) : 1.0; };
                if (a.preWait > 0) ph.pre = frac(gone, a.preWait);
                const double start = gone - a.preWait, length = std::max(0.0, action);
                if (start >= 0) ph.action = frac(start, length);
                if (a.postWait > 0 && start >= length) ph.post = frac(start - length, a.postWait);
            }
        }

        struct Part { double value, weight, frac; QColor color; };
        const Part parts[3] = {{pre, pre * unit, run ? ph.pre : -1, kPreWait},
                               {action, endless ? std::max(1.0, known) : action, run ? ph.action : -1, kLive},
                               {post, post * unit, run ? ph.post : -1, kPostWait}};
        double total = 0;
        for (const Part &part : parts) total += part.weight;
        const QRectF bar = QRectF(opt.rect).adjusted(4, 5, -46, -5);
        // Each part seen, however short: what that takes comes off the longest
        double widths[3], used = 0;
        int longest = 0;
        for (int k = 0; k < 3; ++k) {
            widths[k] = parts[k].weight > 0 ? std::max(3.0, bar.width() * parts[k].weight / std::max(total, 1e-9)) : 0;
            used += widths[k];
            if (widths[k] > widths[longest]) longest = k;
        }
        if (total > 0) widths[longest] = std::max(3.0, widths[longest] - (used - bar.width()));
        p->save();
        QFont small = opt.font;
        small.setPixelSize(10);
        p->setFont(small);
        const QFontMetrics fm(small);
        const bool selected = opt.state & QStyle::State_Selected;
        double x = bar.left();
        for (int k = 0; k < 3; ++k) {
            if (widths[k] <= 0) continue;
            const QRectF r(x, bar.top(), widths[k], bar.height());
            x += widths[k];
            QColor back = parts[k].color;
            back.setAlphaF(parts[k].frac >= 0 && parts[k].frac < 1 ? 0.18f : 0.38f);
            p->fillRect(r, back);
            if (parts[k].frac >= 0 && parts[k].frac < 1) {
                QColor fill = parts[k].color;
                fill.setAlphaF(0.8f);
                p->fillRect(QRectF(r.left(), r.top(), r.width() * parts[k].frac, r.height()), fill);
            }
            const QString number = endless && k == 1 ? QStringLiteral("∞") : seconds(parts[k].value);
            if (fm.horizontalAdvance(number) + 4 <= r.width()) {
                p->setPen(Qt::white);
                p->drawText(r, Qt::AlignCenter, number);
            }
        }
        QFont f = opt.font;
        QColor ink = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        QString text = endless ? QStringLiteral("∞") : seconds(known);
        if (gone >= 0 && (gone < known || endless)) { // on its way: the time gone by, counting up
            f.setBold(true);
            text = seconds(gone);
            if (!selected) ink = Qt::white;
        } else if (total <= 0) {
            text = QStringLiteral("—");
            ink.setAlphaF(0.4f);
        }
        p->setFont(f);
        p->setPen(ink);
        p->drawText(opt.rect.adjusted(0, 0, -6, 0), Qt::AlignVCenter | Qt::AlignRight, text);
        p->restore();
    }

    SequenceWindow *m_w;
};

// A step's pre-wait and post-wait, on its Time cell: Return or a click away keeps them, Esc does not
class StepTimeEditor : public QFrame
{
public:
    StepTimeEditor(QWidget *parent, const QString &suffix, double pre, const QString &action, double post, bool postCounts,
                   std::function<void(double, double)> apply)
        : QFrame(parent, Qt::Popup), m_apply(std::move(apply))
    {
        setObjectName(QStringLiteral("stepTimeEditor"));
        setFrameShape(QFrame::StyledPanel);
        auto *grid = new QGridLayout(this);
        grid->setContentsMargins(8, 8, 8, 8);
        auto field = [&](const char *name, double value) {
            auto *box = new NumberBox;
            box->setObjectName(QLatin1String(name));
            box->setRange(0, 3600);
            box->setDecimals(2);
            box->setSingleStep(0.1);
            box->setSuffix(suffix);
            box->setValue(value);
            return box;
        };
        m_pre = field("stepPre", pre);
        m_post = field("stepPost", post);
        m_post->setEnabled(postCounts);
        m_post->setToolTip(postCounts ? QStringLiteral("Before the next step's GO") : QStringLiteral("Not used: the next step waits for its GO"));
        grid->addWidget(new QLabel(QStringLiteral("Pre-wait")), 0, 0);
        grid->addWidget(m_pre, 0, 1);
        grid->addWidget(new QLabel(QStringLiteral("Action")), 1, 0);
        auto *time = new QLabel(action);
        time->setToolTip(QStringLiteral("The snapshot's time, or the time the timeline plays: set on them"));
        grid->addWidget(time, 1, 1, Qt::AlignRight);
        grid->addWidget(new QLabel(QStringLiteral("Post-wait")), 2, 0);
        grid->addWidget(m_post, 2, 1);
    }
    void focusOn(bool post)
    {
        QDoubleSpinBox *box = post && m_post->isEnabled() ? m_post : m_pre;
        box->setFocus();
        box->selectAll();
    }

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape) m_applied = true;
        if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) close();
        else QFrame::keyPressEvent(e);
    }
    void hideEvent(QHideEvent *e) override
    {
        QFrame::hideEvent(e);
        if (!m_applied) {
            m_applied = true;
            m_pre->interpretText();
            m_post->interpretText();
            m_apply(m_pre->value(), m_post->value());
        }
        deleteLater();
    }

private:
    QDoubleSpinBox *m_pre, *m_post;
    std::function<void(double, double)> m_apply;
    bool m_applied = false;
};

SequenceWindow::SequenceWindow(Engine *engine, QWidget *parent, QUndoStack *undo)
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint), m_undo(undo), m_engine(engine)
{
    setWindowTitle(QStringLiteral("Sequences"));
    resize(1020, 460);
    // The sequence shown, chosen in the menu on top; its steps; its properties under them, above the buttons
    auto *right = new QVBoxLayout(this);
    right->setSpacing(4);
    auto *top = new QHBoxLayout;
    m_sequences = new QComboBox;
    m_sequences->setObjectName(QStringLiteral("sequenceMenu"));
    m_sequences->setEditable(true);
    m_sequences->setInsertPolicy(QComboBox::NoInsert);
    m_sequences->setCompleter(nullptr);
    m_sequences->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_sequences->setToolTip(QStringLiteral("The sequence GO / Stop controls. Other sequences keep playing in the background. Type here to rename it."));
    m_add = new QPushButton(QStringLiteral("+"));
    m_add->setObjectName(QStringLiteral("sequenceNew"));
    m_add->setToolTip(QStringLiteral("New sequence"));
    symbolButton(m_add);
    m_del = new QPushButton(QStringLiteral("−"));
    m_del->setObjectName(QStringLiteral("sequenceDelete"));
    m_del->setToolTip(QStringLiteral("Delete the sequence"));
    symbolButton(m_del);
    m_dup = new QPushButton(QStringLiteral("⧉"));
    m_dup->setObjectName(QStringLiteral("sequenceDuplicate"));
    m_dup->setToolTip(QStringLiteral("Duplicate the sequence"));
    symbolButton(m_dup);
    // What holds for the whole sequence, and the show's clock: out of the way, a click from here
    auto *settings = new QPushButton(QStringLiteral("⚙"));
    settings->setObjectName(QStringLiteral("sequenceSettings"));
    settings->setToolTip(QStringLiteral("The sequence's Loop, Tracking, waits in beats and Timecode, and the show's tempo"));
    symbolButton(settings);
    top->addWidget(m_sequences, 1);
    for (QPushButton *b : {m_add, m_del, m_dup, settings}) top->addWidget(b);
    right->addLayout(top);
    auto *popup = new QFrame(this, Qt::Popup);
    popup->setObjectName(QStringLiteral("sequenceSettingsPopup"));
    popup->setFrameShape(QFrame::StyledPanel);
    popup->setMinimumWidth(400);
    auto *popupLayout = new QVBoxLayout(popup);
    auto *options = new QHBoxLayout;
    connect(settings, &QPushButton::clicked, this, [this, settings, popup] {
        popup->adjustSize();
        QPoint at = settings->mapToGlobal(settings->rect().bottomRight()) - QPoint(popup->width(), 0);
        if (const QScreen *screen = this->screen(); screen && at.y() + popup->height() > screen->availableGeometry().bottom())
            at.setY(settings->mapToGlobal(QPoint(0, 0)).y() - popup->height());
        popup->move(at);
        popup->show();
    });
    auto *flags = new QHBoxLayout;
    auto *clockRows = new QVBoxLayout;
    m_loop = new FlagBox(QStringLiteral("Loop"));
    m_loop->setToolTip(QStringLiteral("GO on the last step plays the first"));
    options->addWidget(m_loop);
    m_tracking = new FlagBox(QStringLiteral("Tracking"));
    m_tracking->setObjectName(QStringLiteral("sequenceTracking"));
    m_tracking->setToolTip(QStringLiteral("A step played out of order (double-click, OSC, GO BACK, the timecode jumping) "
                                          "first gets the show where the steps before it would have left it: the last "
                                          "snapshot before it, then each preset and timeline action after it, at once"));
    options->addWidget(m_tracking);
    m_beats = new FlagBox(QStringLiteral("Waits in beats"));
    m_beats->setObjectName(QStringLiteral("sequenceBeats"));
    m_beats->setToolTip(QStringLiteral("The pre-wait and post-wait of this sequence's steps are counted in beats of the show's clock, "
                                       "taken at its tempo when each step starts"));
    options->addWidget(m_beats);
    options->addStretch();
    popupLayout->addLayout(options);
    popupLayout->addLayout(clockRows);
    // The clock it follows: its steps with a timecode get their GO when it gets there
    m_clock = new QToolButton;
    m_clock->setObjectName(QStringLiteral("sequenceClock"));
    m_clock->setPopupMode(QToolButton::InstantPopup);
    m_clock->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_clock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_clock->setToolTip(QStringLiteral("The clock the steps' timecodes are read on: a soundtrack's or a video's playhead, "
                                       "a Timecode generator (internal, LTC, MTC, ATC). A step with a timecode gets its "
                                       "GO when the clock gets there; after a jump, the last step before it plays."));
    m_clock->setMenu(new QMenu(m_clock));
    m_editMode = new QCheckBox(QStringLiteral("Edit mode"));
    m_editMode->setObjectName(QStringLiteral("sequenceEditMode"));
    m_editMode->setToolTip(
        QStringLiteral("Mutes timecode and auto-follow. Stops this sequence's pending waits. Incoming timecode is ignored; manual GO auditions only "
                       "one cue, without pre-wait, tracking or automatic chaining. The source clock keeps running. "
                       "Unmuting does not replay cues crossed while muted."));
    flags->addWidget(m_editMode);
    connect(m_editMode, &QCheckBox::toggled, this, [this](bool on) {
        if (m_filling)
            return;
        const int index = m_engine->sequencer().current();
        auto sequence = m_engine->sequencer().sequence(index);
        sequence.editMode = on;
        applySequence(index, sequence, QStringLiteral("Sequence Edit Mode"));
        emit edited();
    });
    // The show's musical clock: tempo, tap, start / stop, and where it is
    clockRows->addWidget(m_clock);
    auto *clock = new QHBoxLayout;
    clock->setSpacing(4);
    clockRows->addLayout(clock);
    clock->addWidget(new QLabel(QStringLiteral("Tempo")));
    m_bpm = new NumberBox;
    m_bpm->setObjectName(QStringLiteral("clockBpm"));
    m_bpm->setRange(BeatClock::kMinBpm, BeatClock::kMaxBpm);
    m_bpm->setDecimals(1);
    m_bpm->setSingleStep(1);
    m_bpm->setSuffix(QStringLiteral(" BPM"));
    m_bpm->setToolTip(QStringLiteral("Tempo of the show's clock (OSC /clock/bpm). A tempo measured elsewhere (Ableton Link through a helper, "
                                     "a DAW: OSC /clock/sync) is followed while it comes in"));
    m_tap = new QPushButton(QStringLiteral("Tap"));
    m_tap->setObjectName(QStringLiteral("clockTap"));
    m_tap->setToolTip(QStringLiteral("Tap the tempo (the last taps of the last two seconds)"));
    m_clockRun = new QPushButton(QStringLiteral("▶"));
    m_clockRun->setObjectName(QStringLiteral("clockRun"));
    m_clockRun->setCheckable(true);
    m_clockRun->setToolTip(QStringLiteral("Start the clock from the first beat of a bar / stop it. A stopped clock holds no quantize"));
    m_beatLabel = new QLabel;
    m_beatLabel->setObjectName(QStringLiteral("clockBeat"));
    m_beatLabel->setMinimumWidth(70);
    clock->addWidget(m_bpm);
    clock->addWidget(m_tap);
    clock->addWidget(m_clockRun);
    clock->addStretch();
    connect(m_bpm, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (!m_filling && m_bpm->hasFocus()) m_engine->beatClock().setBpm(v);
    });
    connect(m_tap, &QPushButton::clicked, this, [this] { m_engine->beatClock().tap(); });
    connect(m_clockRun, &QPushButton::clicked, this, [this](bool on) {
        if (on) m_engine->beatClock().start();
        else m_engine->beatClock().stop();
    });
    auto *clockTimer = new QTimer(this);
    clockTimer->setInterval(80);
    connect(clockTimer, &QTimer::timeout, this, &SequenceWindow::pollClock);
    clockTimer->start();
    m_bpm->setValue(m_engine->beatClock().bpm());


    // Steps
    m_status = new QLabel;
    m_status->setTextFormat(Qt::RichText);
    flags->addWidget(m_status, 1);
    flags->addWidget(m_beatLabel);
    right->addWidget(new RecallProgressBar(m_engine));
    m_steps = new QTableWidget(0, ColCount);
    m_steps->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("Target"), QStringLiteral("Pre"),
                                        QStringLiteral("Action"), QStringLiteral("Post"), QStringLiteral("Quantize"),
                                        QStringLiteral("Next"), QStringLiteral("Description"), QStringLiteral("Timecode"), QStringLiteral("Time")});
    m_steps->horizontalHeaderItem(ColTime)->setToolTip(
        QStringLiteral("The step's times on one bar: pre-wait (yellow), action (green), post-wait (blue, with AUTO); at the end, "
                       "the whole time, or the time gone by while it plays\n"
                       "Double-click to change the waits · the action is the snapshot's fade, or the time the timeline plays"));
    m_steps->horizontalHeaderItem(ColAt)->setToolTip(
        QStringLiteral("When the sequence's clock (below the sequences) gets there, the step gets its GO\n"
                       "Seconds, or minutes:seconds (1:02.35); empty: none, the step waits for GO or follows"));
    m_steps->horizontalHeaderItem(ColQuantize)->setToolTip(
        QStringLiteral("A GO waits for the next multiple of this many beats of the show's clock (1: the beat, 4: a 4/4 bar), then "
                       "the pre-wait runs. off: no grid. A stopped clock never gets there: the step goes at once\n"
                       "Choose a Timecode below and this column becomes the steps' timecodes"));
    m_steps->horizontalHeaderItem(ColNext)->setToolTip(
        QStringLiteral("GO: the next step waits for GO (button, Space, OSC)\n"
                       "AUTO: the next step goes once this one's action is over (plus the post-wait)\n"
                       "Double-click to change it. To overlap steps, group them: each keeps its own waits in the group"));
    m_steps->setItemDelegate(new StepDelegate(this));
    // Their values stay in cells of their own, out of sight: the Time column shows them
    for (int c : {ColPre, ColAction, ColPost}) m_steps->setColumnHidden(c, true);
    m_steps->setColumnWidth(ColNext, 56);
    m_steps->setColumnWidth(ColTime, 150);
    m_steps->horizontalHeader()->moveSection(ColNext, 2); // beside the target, before the times
    m_steps->horizontalHeader()->moveSection(m_steps->horizontalHeader()->visualIndex(ColTime), 3);
    // First, when it plays: one column, its timecode on a sequence with a clock and its quantize without
    m_steps->horizontalHeader()->moveSection(m_steps->horizontalHeader()->visualIndex(ColQuantize), 1);
    m_steps->horizontalHeader()->moveSection(m_steps->horizontalHeader()->visualIndex(ColAt), 1);
    m_steps->setColumnWidth(ColAt, 70);
    m_steps->setColumnWidth(ColQuantize, 70);
    m_steps->setColumnHidden(ColAt, true);
    m_steps->verticalHeader()->setDefaultSectionSize(24);
    m_steps->verticalHeader()->setVisible(false);
    m_steps->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_steps->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_steps->horizontalHeader()->setSectionResizeMode(ColSnapshot, QHeaderView::Stretch);
    m_steps->horizontalHeader()->setSectionResizeMode(ColDescription, QHeaderView::Stretch);
    m_steps->horizontalHeader()->setMinimumSectionSize(30);
    m_steps->setColumnWidth(ColNum, 40);
    m_steps->setObjectName(QStringLiteral("sequenceSteps"));
    m_steps->setAcceptDrops(true);
    m_steps->viewport()->setAcceptDrops(true);
    m_steps->viewport()->installEventFilter(this);
    m_steps->setDragEnabled(true);
    m_steps->setDragDropMode(QAbstractItemView::DragDrop);
    m_steps->setDropIndicatorShown(false); // its own line: steps go between rows, and into groups
    m_dropLine = new QFrame(m_steps->viewport());
    m_dropLine->setObjectName(QStringLiteral("stepDropLine"));
    m_dropLine->setStyleSheet(QStringLiteral("background:#4cd964;"));
    m_dropLine->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dropLine->hide();
    m_steps->setContextMenuPolicy(Qt::CustomContextMenu);
    m_steps->setToolTip(QStringLiteral("Double-click a number or ⌘Return: play that step · double-click or Return: edit a cell · double-click a "
                                       "timeline step: its action · Delete: delete · ⌘D: duplicate · ⌘G: group the selected steps\n"
                                       "Right-click: choose a snapshot, preset, timeline or sequence"));
    new ListKeys(m_steps, {[this] {
                               const int row = m_steps->currentRow();
                               if (row < 0) return;
                               if (actionOfRow(row) == kHeaderRow) return toggleFolder(row);
                               if (m_steps->currentColumn() == ColTime) return openTimeEditor(row);
                               if (m_steps->currentColumn() == ColNext) return toggleNext(row);
                               const QTableWidgetItem *cell = m_steps->currentItem();
                               const bool editable = cell && (cell->flags() & Qt::ItemIsEditable);
                               m_steps->editItem(m_steps->item(row, editable ? m_steps->currentColumn() : int(ColDescription)));
                           },
                           [this] { if (m_steps->currentRow() >= 0) emit goToRequested(stepOfRow(m_steps->currentRow())); },
                           [this] { removeSteps(); }, [this] { duplicateSteps(); }, [this] { groupSteps(); }});
    right->addWidget(m_steps, 1);
    m_stepChanges = new QPlainTextEdit;
    m_stepChanges->setObjectName(QStringLiteral("sequenceStepChanges"));
    m_stepChanges->setReadOnly(true);
    m_stepChanges->setFocusPolicy(Qt::NoFocus);
    m_stepChanges->setMaximumHeight(110);
    m_stepChanges->setToolTip(QStringLiteral("Stored changes from the preceding steps in this sequence. Selecting a row never recalls it."));
    m_stepChanges->hide();
    right->addWidget(m_stepChanges);
    connect(m_steps, &QTableWidget::currentCellChanged, this, [this] {
        if (!m_filling) refreshStepChanges();
    });
    auto *stb = new QHBoxLayout;
    m_stepAdd = new QPushButton(QStringLiteral("+ Step"));
    m_stepDel = new QPushButton(QStringLiteral("−"));
    m_stepDel->setToolTip(QStringLiteral("Delete the selected steps"));
    m_up = new QPushButton(QStringLiteral("▲"));
    m_down = new QPushButton(QStringLiteral("▼"));
    for (QPushButton *b : {m_stepAdd, m_stepDel, m_up, m_down}) stb->addWidget(b);
    stb->addStretch();
    m_stop = new QPushButton(QStringLiteral("■ Stop"));
    m_stop->setToolTip(QStringLiteral("The selected sequence: its pre-waits and follows still to come are dropped "
                                      "(the snapshots already running go on; the other sequences keep playing)"));
    m_stop->setEnabled(false);
    stb->addWidget(m_stop);
    connect(m_stop, &QPushButton::clicked, this, &SequenceWindow::stopRequested);
    m_stopAll = new QPushButton(QStringLiteral("■■ Stop all"));
    m_stopAll->setToolTip(QStringLiteral("The pre-waits and follows still to come in every sequence, those playing "
                                         "in the background too (the snapshots already running go on)"));
    m_stopAll->setEnabled(false);
    stb->addWidget(m_stopAll);
    connect(m_stopAll, &QPushButton::clicked, this, &SequenceWindow::stopAllRequested);
    auto *runs = new QTimer(this);
    runs->setInterval(33);
    connect(runs, &QTimer::timeout, this, &SequenceWindow::pollRuns);
    runs->start();
    flags->setSpacing(12);
    for (QCheckBox *box : {static_cast<QCheckBox *>(m_loop), static_cast<QCheckBox *>(m_tracking), static_cast<QCheckBox *>(m_beats), m_editMode})
        box->setMinimumWidth(box->fontMetrics().horizontalAdvance(box->text()) + 28); // with its square
    right->addLayout(flags);
    right->addLayout(stb);

    auto current = [this] { return m_engine->sequencer().current(); };
    connect(m_sequences, qOverload<int>(&QComboBox::activated), this, [this](int r) {
        if (!m_filling && r >= 0) m_engine->sequencer().setCurrent(r);
    });
    connect(m_sequences->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        const int i = m_engine->sequencer().current();
        const QString name = m_sequences->lineEdit()->text().trimmed();
        if (m_filling || i < 0 || name.isEmpty()) return;
        Sequencer::Sequence s = m_engine->sequencer().sequence(i);
        if (s.name == name) return;
        s.name = name;
        applySequence(i, s);
        emit edited();
    });
    connect(m_add, &QPushButton::clicked, this, [this] {
        Sequencer::Sequence s;
        s.name = QStringLiteral("Sequence %1").arg(m_engine->sequencer().count() + 1);
        m_engine->sequencer().setCurrent(m_engine->sequencer().add(s));
        emit edited();
    });
    connect(m_dup, &QPushButton::clicked, this, [this, current] {
        if (current() < 0) return;
        Sequencer::Sequence s = m_engine->sequencer().sequence(current());
        s.name += QStringLiteral(" copy");
        m_engine->sequencer().setCurrent(m_engine->sequencer().add(s, current() + 1));
        emit edited();
    });
    connect(m_del, &QPushButton::clicked, this, [this, current] {
        if (current() < 0) return;
        m_engine->sequencer().remove(current());
        emit edited();
    });
    connect(m_beats, &QCheckBox::toggled, this, [this, current](bool on) {
        if (m_filling || current() < 0) return;
        Sequencer::Sequence s = m_engine->sequencer().sequence(current());
        s.beats = on;
        m_engine->sequencer().setSequence(current(), s);
        emit edited();
        refresh(); // the headers say the unit
    });
    connect(m_clock->menu(), &QMenu::aboutToShow, this, [this, current] {
        QMenu *menu = m_clock->menu();
        menu->clear();
        if (current() < 0) return;
        auto choose = [this, current](quint64 layer, const QString &param) {
            Sequencer::Sequence s = m_engine->sequencer().sequence(current());
            s.clockLayer = layer;
            s.clockParam = param;
            applySequence(current(), s);
            emit edited();
        };
        menu->addAction(QStringLiteral("None"), this, [choose] { choose(0, QString()); });
        menu->addSeparator();
        // The values that tell a time: a media's playhead, a Timecode generator's position
        std::vector<std::pair<quint64, QString>> layers;
        {
            Engine::Lock lk(&m_engine->mutex());
            for (int i = 0; i < m_engine->layerCount(); ++i)
                if (const Layer *l = m_engine->layer(i); l && !l->isGroup) layers.emplace_back(l->id, l->name);
        }
        for (const auto &[id, name] : layers)
            for (const char *path : {"playhead", "tc/position"}) {
                ParamInfo info;
                if (m_engine->parameterInfo(id, QString::fromLatin1(path), &info))
                    menu->addAction(QStringLiteral("%1 › %2").arg(name, info.label.section(QStringLiteral(" › "), -1)), this,
                                    [choose, id, path] { choose(id, QString::fromLatin1(path)); });
            }
    });
    connect(m_tracking, &QCheckBox::toggled, this, [this, current](bool on) {
        if (m_filling || current() < 0) return;
        Sequencer::Sequence s = m_engine->sequencer().sequence(current());
        s.tracking = on;
        applySequence(current(), s);
        emit edited();
    });
    connect(m_loop, &QCheckBox::toggled, this, [this, current](bool on) {
        if (m_filling || current() < 0) return;
        Sequencer::Sequence s = m_engine->sequencer().sequence(current());
        s.loop = on;
        applySequence(current(), s);
        emit edited();
    });
    connect(m_steps, &QTableWidget::itemChanged, this, [this, current](QTableWidgetItem *it) {
        if (m_filling || current() < 0) return;
        Sequencer::Sequence s = m_engine->sequencer().sequence(current());
        Sequencer::Step *found = stepAt(s, it->row());
        if (!found) return;
        Sequencer::Step &st = *found;
        switch (it->column()) {
        case ColDescription: st.description = it->text(); break;
        case ColAt: {
            st.at = parseTimecode(it->text());
            const QSignalBlocker blocker(m_steps);
            it->setText(timecodeText(st.at)); // as it is read
            break;
        }
        case ColPre: st.preWait = std::clamp(it->data(Qt::EditRole).toDouble(), 0.0, 3600.0); break;
        case ColPost: st.postWait = std::clamp(it->data(Qt::EditRole).toDouble(), 0.0, 3600.0); break;
        case ColQuantize: st.quantize = std::clamp(it->data(Qt::EditRole).toDouble(), 0.0, 64.0); break;
        case ColNext: {
            st.next = Sequencer::StepContinue(std::clamp(it->data(Qt::EditRole).toInt(), 0, 1));
            const QSignalBlocker blocker(m_steps);
            it->setToolTip(continueHelp(st.next));
            it->setData(Qt::AccessibleTextRole, continueName(st.next));
            break;
        }
        default: return;
        }
        applySequence(current(), s);
        emit edited();
    });
    connect(m_steps, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col == ColNum) emit goToRequested(stepOfRow(row));
        if (col == ColNext) toggleNext(row);
        if (col == ColTime) { // the side clicked: its wait
            const QRect cell = m_steps->visualItemRect(m_steps->item(row, ColTime));
            openTimeEditor(row, m_steps->viewport()->mapFromGlobal(QCursor::pos()).x() > cell.center().x());
        }
        if (col == ColSnapshot && actionOfRow(row) == kHeaderRow) return toggleFolder(row);
        if (col == ColSnapshot) {
            Sequencer::Sequence s = m_engine->sequencer().sequence(m_engine->sequencer().current());
            const Sequencer::Step *st = stepAt(s, row);
            if (st && !st->actions.empty() && actionOfRow(row) < 0) {
                toggleFold(stepOfRow(row));
            } else if (st && st->timeline) {
                const QRect r = m_steps->visualItemRect(m_steps->item(row, col));
                QMenu menu;
                fillActionMenu(&menu, row);
                menu.exec(m_steps->viewport()->mapToGlobal(r.bottomLeft()));
            }
        }
    });
    connect(m_steps, &QTableWidget::cellClicked, this, [this](int row, int col) { // a group's triangle
        if (col != ColSnapshot) return;
        const int role = m_steps->item(row, col)->data(Qt::UserRole).toInt();
        if (role != 1 && role != 3) return;
        const QRect cell = m_steps->visualItemRect(m_steps->item(row, col));
        const int x = m_steps->viewport()->mapFromGlobal(QCursor::pos()).x() - cell.left();
        if (role == 3 && x < 22) toggleFolder(row);
        else if (role == 1 && x < (m_folders[size_t(stepOfRow(row))] ? 60 : 22)) toggleFold(stepOfRow(row));
    });
    connect(m_steps, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int row = m_steps->rowAt(pos.y());
        QMenu menu;
        if (row >= 0 && !m_steps->selectionModel()->isRowSelected(row, QModelIndex()))
            m_steps->setCurrentCell(row, ColSnapshot);
        fillStepMenu(&menu, row);
        menu.exec(m_steps->viewport()->mapToGlobal(pos));
    });
    connect(m_stepAdd, &QPushButton::clicked, this, [this] {
        const int r = m_steps->currentRow();
        if (r >= 0) insertStep(r, false);
        else addStep(-1, 0);
    });
    connect(m_stepDel, &QPushButton::clicked, this, &SequenceWindow::removeSteps);
    auto *duplicateStep = new QPushButton(QStringLiteral("⧉"));
    duplicateStep->setToolTip(QStringLiteral("Duplicate the selected steps"));
    duplicateStep->setObjectName("sequenceDuplicateSteps");
    symbolButton(duplicateStep);
    stb->insertWidget(stb->indexOf(m_up), duplicateStep);
    connect(duplicateStep, &QPushButton::clicked, this, &SequenceWindow::duplicateSteps);
    auto *group = new QPushButton(QStringLiteral("Group"));
    group->setObjectName("sequenceGroupSteps");
    group->setToolTip(QStringLiteral("A group of the selected steps, or a new one (⌘G): its steps start on one GO, each after its own pre-wait. "
                                     "Drag steps in and out of it"));
    stb->insertWidget(stb->indexOf(m_down) + 1, group);
    connect(group, &QPushButton::clicked, this, &SequenceWindow::groupSteps);
    // GO: from the keyboard's Space too. Neither takes the focus: Space and Return stay with the rest of the panel
    m_back = new QPushButton(QStringLiteral("‹"));
    m_back->setObjectName(QStringLiteral("sequenceBack"));
    m_back->setToolTip(QStringLiteral("GO BACK: the step before (Shift+Space)"));
    symbolButton(m_back);
    m_go = new QPushButton(QStringLiteral("GO ▶"));
    m_go->setObjectName(QStringLiteral("sequenceGo"));
    m_go->setToolTip(QStringLiteral("GO: the next step (Space)"));
    m_go->setStyleSheet("QPushButton { font-weight:bold; background:#2f6b3a; color:white; padding:3px 18px; }"
                        "QPushButton:disabled { background:#2a3a2d; color:#7d8a7f; }");
    for (QPushButton *b : {m_back, m_go}) {
        b->setFocusPolicy(Qt::NoFocus);
        stb->addWidget(b);
    }
    connect(m_go, &QPushButton::clicked, this, &SequenceWindow::goRequested);
    connect(m_back, &QPushButton::clicked, this, &SequenceWindow::backRequested);
    for (auto entry : {std::pair<QKeySequence, int>{QKeySequence(QKeySequence::Copy), 0},
                       {QKeySequence(QKeySequence::Cut), 1},
                       {QKeySequence(QKeySequence::Paste), 2},
                       {QKeySequence(Qt::Key_Insert), 3}}) {
        auto *shortcut = new QShortcut(entry.first, m_steps);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, action = entry.second] {
            if (action < 2)
                copySteps(action == 1);
            else if (action == 2)
                pasteSteps();
            else
                insertStep(std::max(0, m_steps->currentRow()), true);
        });
    }

    connect(m_up, &QPushButton::clicked, this, [this] { moveStep(-1); });
    connect(m_down, &QPushButton::clicked, this, [this] { moveStep(+1); });
    connect(&m_engine->sequencer(), &Sequencer::changed, this, &SequenceWindow::refresh);
    connect(&m_engine->sequencer(), &Sequencer::positionChanged, this, &SequenceWindow::refreshTransport);
    connect(m_engine, &Engine::snapshotsChanged, this, &SequenceWindow::refresh);
    connect(m_engine, &Engine::animationsChanged, this, &SequenceWindow::refresh);
    connect(m_engine, &Engine::presetsChanged, this, &SequenceWindow::refresh);
    refresh();
}

void SequenceWindow::toggleNext(int row)
{
    if (actionOfRow(row) != -1) return;
    if (QTableWidgetItem *cell = m_steps->item(row, ColNext))
        cell->setData(Qt::EditRole, cell->data(Qt::EditRole).toInt() == int(Sequencer::StepContinue::Wait)
                                        ? int(Sequencer::StepContinue::AutoFollow) : int(Sequencer::StepContinue::Wait));
}

void SequenceWindow::openTimeEditor(int row, bool post)
{
    const int cur = m_engine->sequencer().current();
    Sequencer::Sequence s = m_engine->sequencer().sequence(cur);
    const Sequencer::Step *found = stepAt(s, row);
    if (!found) return;
    const Sequencer::Step &st = *found;
    const double length = m_engine->sequencer().stepDuration(st, s.beats ? m_engine->beatClock().secondsPerBeat() : 1.0);
    auto *editor = new StepTimeEditor(this, s.beats ? QStringLiteral(" beats") : QStringLiteral(" s"), st.preWait,
                                      QStringLiteral("%1 s").arg(seconds(length)), st.postWait,
                                      actionOfRow(row) >= 0 || st.next != Sequencer::StepContinue::Wait, [this, cur, row](double pre, double postWait) {
        Sequencer::Sequence edited = m_engine->sequencer().sequence(cur);
        Sequencer::Step *target = stepAt(edited, row);
        if (!target) return;
        Sequencer::Step &step = *target;
        if (step.preWait == pre && step.postWait == postWait) return;
        step.preWait = pre;
        step.postWait = postWait;
        applySequence(cur, edited);
        emit this->edited();
    });
    const QRect cell = m_steps->visualItemRect(m_steps->item(row, ColTime));
    editor->adjustSize();
    QPoint at = m_steps->viewport()->mapToGlobal(cell.bottomLeft());
    if (const QScreen *screen = this->screen(); screen && at.y() + editor->height() > screen->availableGeometry().bottom())
        at.setY(m_steps->viewport()->mapToGlobal(cell.topLeft()).y() - editor->height());
    editor->move(at);
    editor->show();
    editor->focusOn(post);
}

void SequenceWindow::fillStepMenu(QMenu *menu, int row)
{
    if (actionOfRow(row) == kHeaderRow) { // the group itself
        QMenu *mode = menu->addMenu(QStringLiteral("Group mode"));
        mode->addAction(QStringLiteral("All steps together"), this, [this, row] { setGroupTogether(row); });
        QAction *each = mode->addAction(QStringLiteral("One GO per step"));
        each->setCheckable(true);
        each->setChecked(true);
        menu->addAction(QStringLiteral("Ungroup"), this, &SequenceWindow::ungroupStep);
        menu->addSeparator();
        menu->addAction(QStringLiteral("Play its first step"), this, [this, row] { emit goToRequested(stepOfRow(row)); });
        menu->addAction(QStringLiteral("Delete"), this, &SequenceWindow::removeSteps);
        return;
    }
    QMenu *mem = menu->addMenu(row >= 0 ? QStringLiteral("Snapshot of this step") : QStringLiteral("New step with snapshot"));
    for (int i = 0; i < m_engine->snapshotCount(); ++i) {
        const Engine::Snapshot m = m_engine->snapshot(i);
        mem->addAction(QStringLiteral("%1  %2").arg(i + 1).arg(m.name), this, [this, row, id = m.id] {
            if (row >= 0) setStepSnapshot(row, id);
            else addStep(-1, id);
        });
    }
    mem->setEnabled(!mem->isEmpty());
    QMenu *tl = menu->addMenu(row >= 0 ? QStringLiteral("Timeline of this step") : QStringLiteral("New step with timeline"));
    for (int i = 0; i < m_engine->animationCount(); ++i) {
        const Engine::Animation a = m_engine->animation(i);
        tl->addAction(a.name, this, [this, row, id = a.id] {
            if (row >= 0) setStepTimeline(row, id);
            else addStep(-1, 0, id);
        });
    }
    tl->setEnabled(!tl->isEmpty());
    auto *presets = menu->addMenu(row >= 0 ? QStringLiteral("Preset of this step") : QStringLiteral("New step with preset"));
    for (const auto &preset : m_engine->presets())
        presets->addAction(preset.name, this, [this, row, id = preset.id] {
            if (row >= 0) setStepLibraryTarget(row, id, 0); else addStep(-1, 0, 0, id);
        });
    presets->setEnabled(!presets->isEmpty());
    auto *sequences = menu->addMenu(row >= 0 ? QStringLiteral("Play sequence") : QStringLiteral("New step playing sequence"));
    const auto &sq = m_engine->sequencer();
    const quint64 caller = sq.sequenceId(sq.current());
    for (int i = 0; i < sq.count(); ++i) {
        if (i == sq.current()) continue;
        const auto sequence = sq.sequence(i);
        const quint64 id = sq.sequenceId(i);
        QSet<quint64> visited;
        std::function<bool(quint64)> callsBack = [&](quint64 target) {
            if (target == caller) return true;
            if (!target || visited.contains(target)) return false;
            visited.insert(target);
            for (const auto &step : sq.sequence(sq.indexOfSequence(target)).steps)
                if (callsBack(step.sequence)) return true;
            return false;
        };
        auto *action = sequences->addAction(sequence.name, this, [this, row, id] {
            if (row >= 0) setStepLibraryTarget(row, 0, id); else addStep(-1, 0, 0, 0, id);
        });
        const bool cycle = callsBack(id);
        action->setEnabled(!sequence.steps.empty() && !cycle);
        action->setToolTip(cycle ? QStringLiteral("Would create a circular sequence call")
            : QStringLiteral("Starts from the first step, using this sequence's own waits and continuation"));
    }
    sequences->setEnabled(!sequences->isEmpty());
    const Sequencer::Sequence cs = m_engine->sequencer().sequence(m_engine->sequencer().current());
    Sequencer::Sequence shown = cs;
    if (const Sequencer::Step *st = stepAt(shown, row); st && st->timeline)
        fillActionMenu(menu->addMenu(QStringLiteral("Timeline action")), row);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Insert empty step before"), this, [this, row] { row < 0 ? addStep(-1, 0) : insertStep(row, true); });
    menu->addAction(QStringLiteral("Insert empty step after"), this,
                    [this, row] { row < 0 ? addStep(-1, 0) : insertStep(row, false); });
    menu->addAction(QStringLiteral("Paste steps after"), this, &SequenceWindow::pasteSteps)
        ->setEnabled(QApplication::clipboard()->mimeData()->hasFormat("application/x-nyxmap-sequence-steps"));
    if (row >= 0) {
        menu->addAction(QStringLiteral("Copy steps"), this, [this] { copySteps(); });
        menu->addAction(QStringLiteral("Cut steps"), this, [this] { copySteps(true); });
        menu->addAction(QStringLiteral("Duplicate steps"), this, &SequenceWindow::duplicateSteps);
        bool topLevel = true; // a group holds steps, not groups
        for (int selected : selectedSteps()) topLevel = topLevel && actionOfRow(selected) < 0;
        menu->addAction(QStringLiteral("Create Group"), this, &SequenceWindow::groupSteps)->setEnabled(topLevel);
        menu->addAction(QStringLiteral("Create Group, one GO per step"), this, &SequenceWindow::groupStepsOneGoEach)->setEnabled(topLevel);
        const int stepRow = stepOfRow(row);
        const bool isGroup = stepRow >= 0 && stepRow < int(cs.steps.size()) && !cs.steps[size_t(stepRow)].actions.empty();
        if (isGroup) {
            QMenu *mode = menu->addMenu(QStringLiteral("Group mode"));
            QAction *together = mode->addAction(QStringLiteral("All steps together"));
            together->setCheckable(true);
            together->setChecked(true);
            mode->addAction(QStringLiteral("One GO per step"), this, [this, stepRow] { setGroupOneGoEach(stepRow); });
        }
        menu->addAction(QStringLiteral("Ungroup"), this, &SequenceWindow::ungroupStep)->setEnabled(isGroup);
        menu->addSeparator();
        menu->addAction(QStringLiteral("Play this step"), this, [this, row] { emit goToRequested(stepOfRow(row)); });
        auto *load = menu->addAction(QStringLiteral("Load as Next"), this, [this, row] {
            m_engine->sequencer().loadAsNext(m_engine->sequencer().current(), stepOfRow(row));
        });
        load->setToolTip(QStringLiteral("GO plays this step next; the step played and the show stay as they are. With "
                                        "tracking, its GO first plays what the steps before it leave"));
        menu->addAction(QStringLiteral("Delete"), this, &SequenceWindow::removeSteps);
    }
}

// The actions a timeline step can have; the one it has is checked
void SequenceWindow::fillActionMenu(QMenu *menu, int row)
{
    using A = Engine::AnimAction;
    using L = Engine::AnimLoop;
    Sequencer::Sequence s = m_engine->sequencer().sequence(m_engine->sequencer().current());
    const Sequencer::Step *shown = stepAt(s, row);
    if (!shown) return;
    const Sequencer::Step cur = *shown;
    auto set = [this, row](const std::function<void(Sequencer::Step &)> &f) {
        const int c = m_engine->sequencer().current();
        Sequencer::Sequence q = m_engine->sequencer().sequence(c);
        Sequencer::Step *target = stepAt(q, row);
        if (!target) return;
        f(*target);
        applySequence(c, q);
        emit edited();
    };
    for (A a : {A::Play, A::Pause, A::Stop, A::Rewind}) {
        Sequencer::Step x;
        x.action = a;
        QAction *act = menu->addAction(actionName(x), this, [set, a] { set([a](Sequencer::Step &st) { st.action = a; }); });
        act->setCheckable(true);
        act->setChecked(cur.action == a);
    }
    QAction *seek = menu->addAction(QStringLiteral("Seek…"), this, [this, set, cur] {
        bool ok = false;
        const double t = QInputDialog::getDouble(this, QStringLiteral("Seek"), QStringLiteral("Go to (seconds played)"),
                                                 cur.seekTime, 0, 360000, 2, &ok);
        if (ok) set([t](Sequencer::Step &st) { st.action = A::Seek; st.seekTime = t; });
    });
    seek->setCheckable(true);
    seek->setChecked(cur.action == A::Seek);
    QAction *speed = menu->addAction(cur.action == A::Speed ? QStringLiteral("Speed…  (× %1)").arg(cur.speed, 0, 'f', 2)
                                                            : QStringLiteral("Speed…"),
                                     this, [this, set, cur] {
        bool ok = false;
        const double v = QInputDialog::getDouble(this, QStringLiteral("Speed"),
                                                 QStringLiteral("Playback speed of the timeline (1 = normal, 0 = frozen)"),
                                                 cur.action == A::Speed ? cur.speed : 1.0, 0, 10, 2, &ok);
        if (ok) set([v](Sequencer::Step &st) { st.action = A::Speed; st.speed = v; });
    });
    speed->setCheckable(true);
    speed->setChecked(cur.action == A::Speed);
    QMenu *loop = menu->addMenu(cur.action == A::LoopMode ? QStringLiteral("Loop mode  (%1)").arg(actionName(cur))
                                                          : QStringLiteral("Loop mode"));
    auto mode = [set](L l, int n) {
        set([l, n](Sequencer::Step &st) {
            st.action = A::LoopMode;
            st.loop = l;
            st.repeat = n;
        });
    };
    loop->addAction(QStringLiteral("Once"), this, [mode] { mode(L::Once, 0); });
    loop->addAction(QStringLiteral("Loop ∞"), this, [mode] { mode(L::Loop, 0); });
    loop->addAction(QStringLiteral("Loop × N…"), this, [this, mode, cur] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, QStringLiteral("Loop"), QStringLiteral("Passes"), std::max(2, cur.repeat), 1, 100000, 1, &ok);
        if (ok) mode(L::Loop, n);
    });
    loop->addAction(QStringLiteral("Ping-pong ∞"), this, [mode] { mode(L::PingPong, 0); });
    loop->addAction(QStringLiteral("Ping-pong × N…"), this, [this, mode, cur] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, QStringLiteral("Ping-pong"), QStringLiteral("Passes"), std::max(2, cur.repeat), 1, 100000, 1, &ok);
        if (ok) mode(L::PingPong, n);
    });
}

// The steps on their way: their time columns are redrawn while they move (and once more when they are done)
// An empty Timecode cell offers where the sequence's clock is now, else the timecode of the step before it, else 0
QString SequenceWindow::proposedTimecode(int row) const
{
    const Sequencer::Sequence s = m_engine->sequencer().sequence(m_engine->sequencer().current());
    if (!s.clockParam.isEmpty()) {
        const double now = m_engine->parameterValue(s.clockLayer, s.clockParam).toDouble(-1);
        if (now >= 0) return timecodeText(now);
    }
    for (int k = std::min(row, int(s.steps.size())) - 1; k >= 0; --k)
        if (s.steps[size_t(k)].at >= 0) return timecodeText(s.steps[size_t(k)].at);
    return timecodeText(0);
}

void SequenceWindow::pollRuns()
{
    // A sequence can continue in the background while another is edited.
    // Show its activity on the list item without changing the editable name.
    for (int i = 0; i < m_sequences->count(); ++i) {
        const bool active = m_engine->sequencer().active(i);
        if (m_sequences->itemData(i, Qt::UserRole).toBool() == active) continue;
        m_sequences->setItemData(i, active, Qt::UserRole);
        QFont font = m_sequences->font();
        font.setBold(active);
        m_sequences->setItemData(i, font, Qt::FontRole);
        m_sequences->setItemData(i, active ? QColor(90, 215, 155) : palette().color(QPalette::Text), Qt::ForegroundRole);
        m_sequences->setItemData(i, active ? QStringLiteral("Cues running (including in the background). Select to control this sequence.")
                                           : QStringLiteral("Select to edit or GO this sequence. Other sequences keep playing."), Qt::ToolTipRole);
    }
    // Always update Stop: after selecting an idle sequence both run lists may be
    // empty, but the button may still be enabled for the former selection.
    m_stop->setEnabled(m_engine->sequencer().running());
    m_stopAll->setEnabled(m_engine->sequencer().anyActive());
    m_go->setEnabled(m_engine->sequencer().next() >= 0);
    m_back->setEnabled(m_engine->sequencer().previous() >= 0);
    std::vector<Sequencer::Run> runs = m_engine->sequencer().runs();
    const int position = m_engine->sequencer().position(), upcoming = m_engine->sequencer().next();
    if (runs.empty() && m_runs.empty() && position == m_position && upcoming == m_upcoming) return;
    m_runs = std::move(runs);
    m_position = position;
    m_upcoming = upcoming;
    if (isVisible()) m_steps->viewport()->update();
}

void SequenceWindow::setModified(bool on)
{
    if (on == m_modified) return;
    m_modified = on;
    refreshTransport();
}

// The clock as it is: its tempo (unless being typed), whether it runs, the beat in the bar, where the tempo comes from
void SequenceWindow::pollClock()
{
    if (!isVisible()) return;
    const BeatClock &clock = m_engine->beatClock();
    const bool running = clock.running();
    if (!m_bpm->hasFocus() && std::abs(m_bpm->value() - clock.bpm()) > 0.049) {
        const QSignalBlocker blocker(m_bpm);
        m_bpm->setValue(clock.bpm());
    }
    if (m_clockRun->isChecked() != running) {
        const QSignalBlocker blocker(m_clockRun);
        m_clockRun->setChecked(running);
    }
    m_clockRun->setText(running ? QStringLiteral("■") : QStringLiteral("▶"));
    QString dots;
    if (running) {
        const int inBar = clock.beatInBar();
        for (int b = 0; b < clock.beatsPerBar(); ++b) dots += b == inBar ? QStringLiteral("● ") : QStringLiteral("○ ");
    }
    m_beatLabel->setText(clock.source() == BeatClock::Source::External ? QStringLiteral("ext ") + dots : dots);
}

namespace {
using CueValues = QMap<QPair<QString, QString>, QJsonValue>;

QString cueValueText(const QJsonValue &value)
{
    if (value.isUndefined()) return QStringLiteral("—");
    if (value.isArray()) return QStringLiteral("[%1 items]").arg(value.toArray().size());
    if (value.isObject()) {
        const auto object = value.toObject();
        const auto path = object.value("path").toString();
        if (!path.isEmpty()) return path;
        return object.value("type").toString(QStringLiteral("{settings}"));
    }
    if (value.isString()) return value.toString().simplified();
    const QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}
} // namespace

void SequenceWindow::refreshStepChanges()
{
    const int row = m_steps->currentRow(), index = stepOfRow(row), child = actionOfRow(row);
    const auto sequence = m_engine->sequencer().sequence(m_engine->sequencer().current());
    const bool selected = index >= 0 && index < int(sequence.steps.size()) && child != kHeaderRow;
    m_stepChanges->setVisible(selected);
    if (!selected) return;

    CueValues values;
    QMap<QString, QString> owners;
    QStringList actions;
    auto layerOwner = [&](quint64 id) {
        if (!id) return QStringLiteral("Composition");
        const QString key = QStringLiteral("Layer %1").arg(id);
        if (!owners.contains(key)) {
            Engine::Lock lock(&m_engine->mutex());
            const auto *layer = m_engine->layer(m_engine->indexOfId(id));
            if (layer) owners[key] = layer->name;
        }
        return key;
    };
    auto put = [&](const QString &owner, const QJsonObject &params) {
        for (auto it = params.begin(); it != params.end(); ++it)
            values[{owner, it.key()}] = it.value();
    };
    auto apply = [&](const Sequencer::Step &step, bool report) {
        if (step.preset) {
            const int i = m_engine->indexOfPreset(step.preset);
            if (i < 0) { if (report) actions << stepTarget(m_engine, step); return; }
            const auto preset = m_engine->preset(i);
            for (const auto &value : preset.values)
                values[{layerOwner(value.layer), value.path}] = value.value;
            put(QStringLiteral("Preset structure"), preset.categories);
            put(QStringLiteral("Mapping"), preset.geometry);
        } else if (step.snapshot) {
            const int i = m_engine->indexOfSnapshot(step.snapshot);
            if (i < 0) { if (report) actions << stepTarget(m_engine, step); return; }
            const auto snapshot = m_engine->snapshot(i);
            for (const auto &entry : snapshot.layers) {
                const auto layer = entry.toObject();
                if (!layer.value("included").toBool(true) || layer.value("viewport").toBool()) continue;
                const QString owner = layerOwner(layer.value("id").toString().toULongLong());
                put(owner, layer.value("params").toObject());
                for (const QString &field : {QStringLiteral("enable"), QStringLiteral("source"), QStringLiteral("fx"),
                                             QStringLiteral("parent"), QStringLiteral("mesh"), QStringLiteral("masks"),
                                             QStringLiteral("matte"), QStringLiteral("viewports"), QStringLiteral("anims")}) {
                    if (layer.contains(field)) values[{owner, field}] = layer.value(field);
                }
            }
            if (snapshot.composition.value("included").toBool(true))
                for (const QString &field : {QStringLiteral("opacity"), QStringLiteral("volume")})
                    if (snapshot.composition.contains(field))
                        values[{QStringLiteral("Composition"), field}] = snapshot.composition.value(field);
            for (const auto &[kind, entries] : {std::pair{QStringLiteral("Interaction"), snapshot.interactions},
                                               std::pair{QStringLiteral("Fixture"), snapshot.fixtures}}) {
                for (const auto &entry : entries) {
                    const auto object = entry.toObject();
                    if (!object.value("included").toBool(true)) continue;
                    const QString owner = kind + QLatin1Char(' ') + object.value("id").toString();
                    owners[owner] = object.value("name").toString(owner);
                    put(owner, object.value("params").toObject());
                    if (object.contains("enabled")) values[{owner, QStringLiteral("enabled")}] = object.value("enabled");
                }
            }
        } else if (step.timeline || step.sequence) {
            if (report) actions << stepTarget(m_engine, step);
            if (step.sequence) values.clear(); // its execution is independent of this cue list
            const int timeline = m_engine->indexOfAnimation(step.timeline);
            if (timeline >= 0)
                for (const auto &track : m_engine->animation(timeline).tracks)
                    if (track.enabled)
                        for (const auto &path : track.drivenParams()) values.remove({layerOwner(track.layer), path});
        }
    };
    auto applyStep = [&](const Sequencer::Step &step, bool report) {
        if (step.actions.empty()) apply(step, report);
        else for (const auto &action : step.actions) apply(action, report);
    };
    for (int i = 0; i < index; ++i) applyStep(sequence.steps[size_t(i)], false);
    const CueValues before = values;
    const auto &step = sequence.steps[size_t(index)];
    if (child >= 0 && child < int(step.actions.size())) apply(step.actions[size_t(child)], true);
    else applyStep(step, true);

    QStringList lines{index == 0 ? QStringLiteral("Changes · first GO")
                                : QStringLiteral("Changes · compared with the state after GO %1").arg(index)};
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const QJsonValue old = before.value(it.key(), QJsonValue::Undefined);
        if (old == it.value()) continue;
        const auto &[owner, path] = it.key();
        const QString afterText = cueValueText(it.value());
        const QString beforeText = cueValueText(old);
        lines << QStringLiteral("%1 · %2    %3").arg(owners.value(owner, owner), path,
            beforeText == afterText ? QStringLiteral("changed") : beforeText + QStringLiteral(" → ") + afterText);
    }
    lines.append(actions);
    if (lines.size() == 1) lines << QStringLiteral("No stored changes.");
    const QString text = lines.join(QLatin1Char('\n'));
    if (m_stepChanges->toPlainText() != text) m_stepChanges->setPlainText(text);
}

void SequenceWindow::refresh()
{
    m_filling = true;
    const int n = m_engine->sequencer().count(), cur = m_engine->sequencer().current();
    m_shownSequence = m_engine->sequencer().sequenceId(cur);
    QStringList names;
    for (int i = 0; i < n; ++i) names << m_engine->sequencer().sequence(i).name;
    QStringList shown;
    for (int i = 0; i < m_sequences->count(); ++i) shown << m_sequences->itemText(i);
    if (names != shown) { // not under a name being typed
        m_sequences->clear();
        m_sequences->addItems(names);
    }
    if (m_sequences->currentIndex() != cur) m_sequences->setCurrentIndex(cur);
    m_sequences->setEnabled(n > 0);
    const Sequencer::Sequence s = m_engine->sequencer().sequence(cur);
    m_loop->setChecked(s.loop);
    m_beats->setChecked(s.beats);
    const QString unit = s.beats ? QStringLiteral("beats") : QStringLiteral("seconds");
    m_steps->horizontalHeaderItem(ColPre)->setToolTip(QStringLiteral("In %1, between the step's GO (and its quantize) and its snapshot").arg(unit));
    m_tracking->setChecked(s.tracking);
    QString clock = QStringLiteral("none");
    if (!s.clockParam.isEmpty()) {
        ParamInfo info;
        QString owner = QStringLiteral("Composition");
        if (s.clockLayer) {
            Engine::Lock lk(&m_engine->mutex());
            const Layer *l = m_engine->layer(m_engine->indexOfId(s.clockLayer));
            owner = l ? l->name : QStringLiteral("?");
        }
        clock = m_engine->parameterInfo(s.clockLayer, s.clockParam, &info)
                    ? QStringLiteral("%1 › %2").arg(owner, info.label.section(QStringLiteral(" › "), -1))
                    : QStringLiteral("%1 (gone)").arg(s.clockParam);
    }
    m_clock->setText(QStringLiteral("Timecode: %1%2").arg(clock, s.editMode ? QStringLiteral(" · MUTED") : QString()));
    m_steps->setColumnHidden(ColAt, s.clockParam.isEmpty());
    m_steps->setColumnHidden(ColQuantize, !s.clockParam.isEmpty());
    m_editMode->setChecked(s.editMode);
    m_editMode->setEnabled(cur >= 0);
    for (QWidget *w : std::initializer_list<QWidget *>{m_dup, m_del, m_loop, m_beats, m_tracking, m_clock, m_stepDel, m_up, m_down}) // steps: a first one makes a sequence
        w->setEnabled(cur >= 0);
    const int keep = m_steps->currentRow();
    m_rows.clear();
    m_folders.clear();
    for (int k = 0; k < int(s.steps.size()); ++k) {
        const quint64 folder = s.steps[size_t(k)].folder;
        m_folders.push_back(folder);
        if (folder && (k == 0 || s.steps[size_t(k - 1)].folder != folder)) m_rows.push_back({k, kHeaderRow});
        if (folder && std::count(s.closedFolders.begin(), s.closedFolders.end(), folder)) continue;
        m_rows.push_back({k, -1});
        if (s.steps[size_t(k)].open)
            for (int a = 0; a < int(s.steps[size_t(k)].actions.size()); ++a) m_rows.push_back({k, a});
    }
    m_steps->setRowCount(int(m_rows.size()));
    const int pos = m_engine->sequencer().position();
    m_position = pos;
    m_upcoming = m_engine->sequencer().next();
    for (int row = 0; row < int(m_rows.size()); ++row) {
        const int k = m_rows[size_t(row)].step, child = m_rows[size_t(row)].action;
        if (child == kHeaderRow) { // a group played one GO at a time: its row holds no time of its own
            int first, last;
            folderRange(s, k, &first, &last);
            const bool closed = std::count(s.closedFolders.begin(), s.closedFolders.end(), s.steps[size_t(k)].folder);
            for (int c = 0; c < ColCount; ++c) {
                auto *cell = new QTableWidgetItem;
                cell->setFlags(kRowFlags);
                m_steps->setItem(row, c, cell);
            }
            auto *title = m_steps->item(row, ColSnapshot);
            title->setText(QStringLiteral("%1  Group · %2 GOs").arg(closed ? QStringLiteral("▸") : QStringLiteral("▾")).arg(last - first + 1));
            title->setToolTip(QStringLiteral("A group of %1 steps played one GO per step").arg(last - first + 1));
            title->setData(Qt::UserRole, 3);
            setStepOutline(title, QStringLiteral("GO Group"), QColor(185, 142, 232));
            QFont bold = title->font();
            bold.setBold(true);
            title->setFont(bold);
            continue;
        }
        const Sequencer::Step &st = child < 0 ? s.steps[size_t(k)] : s.steps[size_t(k)].actions[size_t(child)];
        const bool isGroup = child < 0 && !st.actions.empty();
        auto *num = new QTableWidgetItem(child < 0 ? QString::number(k + 1) : QStringLiteral("%1.%2").arg(k + 1).arg(child + 1));
        num->setFlags(kRowFlags);
        num->setTextAlignment(Qt::AlignCenter);
        QString target = stepTarget(m_engine, st);
        if (isGroup && st.open) target = QStringLiteral("▾  Group · %1 steps").arg(st.actions.size());
        else if (isGroup) target = QStringLiteral("▸  ") + target;
        else if (child >= 0) target = QStringLiteral("      ") + target;
        const bool inFolder = s.steps[size_t(k)].folder != 0;
        if (inFolder) target = QStringLiteral("      ") + target;
        auto *mem = new QTableWidgetItem(target);
        mem->setFlags(kRowFlags);
        mem->setData(Qt::UserRole, isGroup ? 1 : child >= 0 ? 2 : inFolder ? 4 : 0);
        if (isGroup) setStepOutline(mem, QStringLiteral("Action Group"), QColor(185, 142, 232));
        else if (st.sequence) setStepOutline(mem, QStringLiteral("Sequence"), QColor(100, 208, 173));
        else if (st.preset) setStepOutline(mem, QStringLiteral("Preset"), QColor(232, 187, 103));
        else if (st.timeline) setStepOutline(mem, QStringLiteral("Timeline"), QColor(86, 196, 255));
        else if (st.snapshot) setStepOutline(mem, QStringLiteral("Snapshot"), QColor(241, 141, 156));
        if (targetGone(m_engine, st)) mem->setForeground(QColor(255, 120, 100));
        else if (st.timeline) mem->setForeground(QColor(86, 196, 255));
        auto *pre = new QTableWidgetItem;
        pre->setData(Qt::EditRole, st.preWait);
        auto *action = new QTableWidgetItem;
        // A timeline played without end: no time to wait for, shown as ∞
        const int ai = st.timeline ? m_engine->indexOfAnimation(st.timeline) : -1;
        const bool endless = ai >= 0 && st.action == Engine::AnimAction::Play && !std::isfinite(m_engine->animation(ai).length());
        const double length = m_engine->sequencer().stepDuration(st, s.beats ? m_engine->beatClock().secondsPerBeat() : 1.0);
        action->setData(Qt::EditRole, endless || !std::isfinite(length) ? -1.0 : length); // ∞: a frozen timeline too
        action->setFlags(kRowFlags);
        auto *post = new QTableWidgetItem;
        post->setData(Qt::EditRole, st.postWait);
        auto *next = new QTableWidgetItem;
        next->setFlags(kRowFlags);
        next->setData(Qt::EditRole, int(st.next));
        next->setToolTip(child < 0 ? continueHelp(st.next) : QStringLiteral("The group says what comes next"));
        next->setData(Qt::AccessibleTextRole, continueName(st.next));
        m_steps->setItem(row, ColNum, num);
        m_steps->setItem(row, ColSnapshot, mem);
        m_steps->setItem(row, ColPre, pre);
        m_steps->setItem(row, ColAction, action);
        m_steps->setItem(row, ColPost, post);
        auto *quantize = new QTableWidgetItem;
        quantize->setData(Qt::EditRole, st.quantize);
        quantize->setTextAlignment(Qt::AlignCenter);
        m_steps->setItem(row, ColQuantize, quantize);
        m_steps->setItem(row, ColNext, next);
        m_steps->setItem(row, ColDescription, new QTableWidgetItem(st.description));
        auto *time = new QTableWidgetItem;
        time->setFlags(kRowFlags);
        const QString unitName = s.beats ? QStringLiteral(" beats") : QStringLiteral(" s");
        const QString times = QStringLiteral("Pre-wait %1%4 · Action %2 s · Post-wait %3")
                                  .arg(seconds(st.preWait), std::isfinite(length) && !endless ? seconds(length) : QStringLiteral("∞"),
                                       child < 0 && st.next == Sequencer::StepContinue::Wait ? QStringLiteral("not used") : seconds(st.postWait) + unitName, unitName);
        time->setToolTip(times);
        time->setData(Qt::AccessibleTextRole, times);
        m_steps->setItem(row, ColTime, time);
        auto *at = new QTableWidgetItem(timecodeText(st.at));
        at->setTextAlignment(Qt::AlignVCenter | Qt::AlignRight);
        m_steps->setItem(row, ColAt, at);
        if (child >= 0) { // the group has its GO: the waits and the description are its steps' own
            quantize->setFlags(kRowFlags);
            at->setFlags(kRowFlags);
            quantize->setText(QString());
            quantize->setData(Qt::EditRole, QVariant());
        }
        if (k == pos && child < 0) { // the step played: green, orange once something changed since
            const QColor c = m_modified ? QColor(QString::fromLatin1(kChanged)) : kLive;
            num->setText(QStringLiteral("▶ %1").arg(k + 1));
            for (QTableWidgetItem *it : {num, mem}) {
                QFont f = it->font();
                f.setBold(true);
                it->setFont(f);
                it->setForeground(c);
            }
        }
    }
    if (keep >= 0 && keep < m_steps->rowCount()) m_steps->setCurrentCell(keep, ColSnapshot);
    refreshTransport();
    m_filling = false;
    refreshStepChanges();
}

void SequenceWindow::addStep(int at, quint64 snapshot, quint64 timeline, quint64 preset, quint64 sequence, quint64 folder)
{
    int cur = m_engine->sequencer().current();
    if (cur < 0) { // a first sequence for it
        Sequencer::Sequence s;
        s.name = QStringLiteral("Sequence 1");
        cur = m_engine->sequencer().add(s);
        m_engine->sequencer().setCurrent(cur);
    }
    Sequencer::Sequence s = m_engine->sequencer().sequence(cur);
    if (at < 0 || at > int(s.steps.size())) at = int(s.steps.size());
    Sequencer::Step st;
    st.snapshot = timeline ? 0 : snapshot;
    st.timeline = timeline; // Play, by default
    st.preset = preset;
    st.sequence = sequence;
    st.folder = folder;
    s.steps.insert(s.steps.begin() + at, st);
    // The step played keeps being the same one
    const int pos = m_engine->sequencer().position();
    applySequence(cur, s);
    if (pos >= at) m_engine->sequencer().setPosition(pos + 1);
    m_steps->setCurrentCell(rowOf(at), ColSnapshot);
    emit edited();
}

int SequenceWindow::rowOf(int step, int action) const
{
    for (int row = 0; row < int(m_rows.size()); ++row)
        if (m_rows[size_t(row)].step == step && m_rows[size_t(row)].action == action) return row;
    // In a folded group played one GO at a time: its row
    if (action == -1 && step >= 0 && step < int(m_folders.size()) && m_folders[size_t(step)])
        for (int row = 0; row < int(m_rows.size()); ++row)
            if (m_rows[size_t(row)].action == kHeaderRow && m_folders[size_t(m_rows[size_t(row)].step)] == m_folders[size_t(step)]) return row;
    return -1;
}

Sequencer::Step *SequenceWindow::stepAt(Sequencer::Sequence &s, int row) const
{
    const int step = stepOfRow(row), action = actionOfRow(row);
    if (step < 0 || step >= int(s.steps.size())) return nullptr;
    if (action == kHeaderRow) return nullptr;
    Sequencer::Step &st = s.steps[size_t(step)];
    if (action < 0) return &st;
    return action < int(st.actions.size()) ? &st.actions[size_t(action)] : nullptr;
}

// What a row given a target acts on: its step; on a group's row, a new step at its end
Sequencer::Step *SequenceWindow::targetAt(Sequencer::Sequence &s, int row) const
{
    Sequencer::Step *st = stepAt(s, row);
    if (!st || st->actions.empty()) return st;
    if (st->actions.size() >= 64) return nullptr;
    st->open = true;
    st->actions.emplace_back();
    return &st->actions.back();
}

void SequenceWindow::setStepSnapshot(int row, quint64 snapshot)
{
    const int cur = m_engine->sequencer().current();
    Sequencer::Sequence s = m_engine->sequencer().sequence(cur);
    Sequencer::Step *st = targetAt(s, row);
    if (!st) return;
    st->snapshot = snapshot;
    st->timeline = st->preset = st->sequence = 0;
    applySequence(cur, s);
    emit edited();
}

void SequenceWindow::setStepTimeline(int row, quint64 timeline)
{
    const int cur = m_engine->sequencer().current();
    Sequencer::Sequence s = m_engine->sequencer().sequence(cur);
    Sequencer::Step *st = targetAt(s, row);
    if (!st) return;
    if (!st->timeline) st->action = Engine::AnimAction::Play; // a step that recalled a snapshot: Play
    st->timeline = timeline;
    st->snapshot = st->preset = st->sequence = 0;
    applySequence(cur, s);
    emit edited();
}

void SequenceWindow::setStepLibraryTarget(int row, quint64 preset, quint64 sequence)
{
    const int cur = m_engine->sequencer().current();
    auto s = m_engine->sequencer().sequence(cur);
    Sequencer::Step *st = targetAt(s, row);
    if (!st) return;
    st->snapshot = st->timeline = 0;
    st->preset = preset;
    st->sequence = sequence;
    applySequence(cur, s);
    emit edited();
}

// A new empty step by a row: beside it, in its group when it is a step of one
void SequenceWindow::insertStep(int row, bool before)
{
    const int step = stepOfRow(row), action = actionOfRow(row);
    const int cur = m_engine->sequencer().current();
    Sequencer::Sequence s = m_engine->sequencer().sequence(cur);
    if (action == kHeaderRow) { // before the group, or at its start; when it is folded, after it
        int first, last;
        folderRange(s, step, &first, &last);
        const quint64 folder = s.steps[size_t(step)].folder;
        if (before) return addStep(first, 0);
        if (std::count(s.closedFolders.begin(), s.closedFolders.end(), folder)) return addStep(last + 1, 0);
        return addStep(first, 0, 0, 0, 0, folder);
    }
    if (action < 0)
        return addStep(step < 0 ? -1 : step + (before ? 0 : 1), 0, 0, 0, 0, step < 0 ? 0 : s.steps[size_t(step)].folder);
    auto &actions = s.steps[size_t(step)].actions;
    if (actions.size() >= 64) return;
    const int at = action + (before ? 0 : 1);
    actions.emplace(actions.begin() + at);
    applySequence(cur, s);
    m_steps->setCurrentCell(rowOf(step, at), ColSnapshot);
    emit edited();
}

void SequenceWindow::toggleFolder(int row)
{
    if (actionOfRow(row) != kHeaderRow) return;
    const int cur = m_engine->sequencer().current();
    Sequencer::Sequence s = m_engine->sequencer().sequence(cur);
    const quint64 folder = s.steps[size_t(stepOfRow(row))].folder;
    const auto found = std::find(s.closedFolders.begin(), s.closedFolders.end(), folder);
    if (found != s.closedFolders.end()) s.closedFolders.erase(found);
    else s.closedFolders.push_back(folder);
    m_engine->sequencer().setSequence(cur, s); // kept (saved), not an undo step
    emit edited();
}

void SequenceWindow::setGroupOneGoEach(int step)
{
    const int index = m_engine->sequencer().current();
    auto sequence = m_engine->sequencer().sequence(index);
    if (step < 0 || step >= int(sequence.steps.size()) || sequence.steps[size_t(step)].actions.empty()) return;
    const auto group = sequence.steps[size_t(step)];
    const quint64 folder = newFolderId(sequence);
    auto steps = group.actions;
    for (auto &st : steps) {
        st.runtimeId = 0;
        st.next = Sequencer::StepContinue::Wait;
        st.folder = folder;
    }
    steps.front().runtimeId = group.runtimeId;
    steps.front().preWait += group.preWait;
    steps.front().at = group.at;
    steps.front().quantize = group.quantize;
    if (!group.description.isEmpty()) steps.front().description = group.description;
    steps.back().postWait += group.postWait;
    steps.back().next = group.next;
    sequence.steps.erase(sequence.steps.begin() + step);
    sequence.steps.insert(sequence.steps.begin() + step, steps.begin(), steps.end());
    applySequence(index, sequence, QStringLiteral("Group Mode"));
    m_steps->setCurrentCell(rowOf(step, kHeaderRow), ColSnapshot);
    emit edited();
}

void SequenceWindow::setGroupTogether(int headerRow)
{
    if (actionOfRow(headerRow) != kHeaderRow) return;
    const auto sequence = m_engine->sequencer().sequence(m_engine->sequencer().current());
    int first, last;
    folderRange(sequence, stepOfRow(headerRow), &first, &last);
    QList<int> steps;
    for (int k = first; k <= last; ++k) steps << k;
    groupRange(steps, true);
}

void SequenceWindow::groupStepsOneGoEach()
{
    const int index = m_engine->sequencer().current();
    if (index < 0) return;
    auto sequence = m_engine->sequencer().sequence(index);
    QList<int> rows;
    for (int row : selectedSteps()) {
        if (actionOfRow(row) != -1) return; // a group holds steps, not groups of its own kind
        rows << stepOfRow(row);
    }
    const quint64 folder = newFolderId(sequence);
    if (rows.empty()) return addStep(-1, 0, 0, 0, 0, folder);
    if (rows.back() - rows.front() + 1 != rows.size()) return;
    for (int k : rows) sequence.steps[size_t(k)].folder = folder;
    normalizeFolders(sequence);
    applySequence(index, sequence, QStringLiteral("Create Group"));
    m_steps->setCurrentCell(rowOf(rows.front(), kHeaderRow), ColSnapshot);
    emit edited();
}

void SequenceWindow::toggleFold(int step)
{
    const int cur = m_engine->sequencer().current();
    Sequencer::Sequence s = m_engine->sequencer().sequence(cur);
    if (step < 0 || step >= int(s.steps.size()) || s.steps[size_t(step)].actions.empty()) return;
    s.steps[size_t(step)].open = !s.steps[size_t(step)].open;
    m_engine->sequencer().setSequence(cur, s); // kept (saved), not an undo step
    emit edited();
}

void SequenceWindow::removeSteps()
{
    const int cur = m_engine->sequencer().current();
    Sequencer::Sequence s = m_engine->sequencer().sequence(cur);
    QList<int> rows = selectedSteps();
    if (rows.isEmpty()) return;
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    // The steps of groups first, a group left empty going too; then the steps
    std::set<int, std::greater<int>> steps;
    for (int r : rows) {
        const int step = stepOfRow(r), action = actionOfRow(r);
        if (step < 0 || step >= int(s.steps.size())) continue;
        if (action == kHeaderRow) { // all its steps
            int first, last;
            folderRange(s, step, &first, &last);
            for (int k = first; k <= last; ++k) steps.insert(k);
            continue;
        }
        if (action < 0) {
            steps.insert(step);
            continue;
        }
        auto &actions = s.steps[size_t(step)].actions;
        if (action < int(actions.size())) actions.erase(actions.begin() + action);
        if (actions.empty()) steps.insert(step);
    }
    int pos = m_engine->sequencer().position();
    for (int r : steps) {
        s.steps.erase(s.steps.begin() + r);
        if (pos == r) pos = r - 1; // the step played is gone: as if the one before was
        else if (pos > r) --pos;
    }
    applySequence(cur, s);
    m_engine->sequencer().setPosition(pos);
    emit edited();
}

void SequenceWindow::moveStep(int delta)
{
    const int cur = m_engine->sequencer().current();
    const Sequencer::Sequence s = m_engine->sequencer().sequence(cur);
    const int r = m_steps->currentRow(), step = stepOfRow(r), action = actionOfRow(r);
    if (step < 0 || step >= int(s.steps.size())) return;
    const Sequencer::Step &st = s.steps[size_t(step)];
    const auto folded = [&](quint64 folder) { return std::count(s.closedFolders.begin(), s.closedFolders.end(), folder) > 0; };
    int first = step, last = step;
    if (st.folder) folderRange(s, step, &first, &last);
    if (action == kHeaderRow) { // the whole group, past what is beside it
        if (delta < 0 && first == 0) return;
        if (delta > 0 && last + 1 >= int(s.steps.size())) return;
        int from, to;
        folderRange(s, delta < 0 ? first - 1 : last + 1, &from, &to);
        if (!s.steps[size_t(delta < 0 ? first - 1 : last + 1)].folder) from = to = delta < 0 ? first - 1 : last + 1;
        moveRowsTo({r}, delta < 0 ? from : to + 1, -1);
        return;
    }
    if (action < 0) {
        const int to = step + delta;
        if (st.folder && (delta < 0 ? step == first : step == last)) { // out of its group: where it is, past its row
            moveRowsTo({r}, delta < 0 ? step : step + 1, -1, 0);
            return;
        }
        if (to >= 0 && to < int(s.steps.size()) && s.steps[size_t(to)].folder && s.steps[size_t(to)].folder != st.folder) {
            const quint64 beside = s.steps[size_t(to)].folder;
            int from, end;
            folderRange(s, to, &from, &end);
            if (folded(beside)) moveRowsTo({r}, delta < 0 ? from : end + 1, -1, 0);
            else moveRowsTo({r}, delta < 0 ? step : step + 1, -1, beside); // into it, where it is
            return;
        }
    }
    if (action >= 0) { // in its group, then out of it
        const int count = int(st.actions.size());
        if (delta < 0) action > 0 ? moveRowsTo({r}, step, action - 1) : moveRowsTo({r}, step, -1, st.folder);
        else action < count - 1 ? moveRowsTo({r}, step, action + 2) : moveRowsTo({r}, step + 1, -1, st.folder);
        return;
    }
    const int to = step + delta;
    if (to < 0 || to >= int(s.steps.size())) return;
    const Sequencer::Step &other = s.steps[size_t(to)];
    const bool enters = st.actions.empty() && !other.actions.empty() && other.open; // into the open group beside it
    if (delta < 0) enters ? moveRowsTo({r}, to, int(other.actions.size())) : moveRowsTo({r}, to, -1, st.folder);
    else enters ? moveRowsTo({r}, to, 0) : moveRowsTo({r}, to + 1, -1, st.folder);
}

void SequenceWindow::moveRowsTo(QList<int> rows, int top, int action, quint64 folder)
{
    const int cur = m_engine->sequencer().current();
    Sequencer::Sequence s = m_engine->sequencer().sequence(cur);
    std::set<int> movedSteps;
    std::set<std::pair<int, int>> movedActions;
    std::sort(rows.begin(), rows.end());
    bool hasHeader = false; // a whole group played one GO at a time goes as it is
    for (int r : rows) {
        if (stepOfRow(r) < 0 || actionOfRow(r) >= 0) continue;
        if (actionOfRow(r) == kHeaderRow) {
            int first, last;
            folderRange(s, stepOfRow(r), &first, &last);
            for (int k = first; k <= last; ++k) movedSteps.insert(k);
            hasHeader = true;
        } else {
            movedSteps.insert(stepOfRow(r));
        }
    }
    for (int r : rows)
        if (actionOfRow(r) >= 0 && !movedSteps.count(stepOfRow(r))) movedActions.insert({stepOfRow(r), actionOfRow(r)});
    std::vector<Sequencer::Step> moved; // in the order of the list
    bool hasGroup = false;
    for (int k = 0; k < int(s.steps.size()); ++k) {
        if (movedSteps.count(k)) {
            moved.push_back(s.steps[size_t(k)]);
            hasGroup = hasGroup || !s.steps[size_t(k)].actions.empty();
        }
        for (int a = 0; a < int(s.steps[size_t(k)].actions.size()); ++a)
            if (movedActions.count({k, a})) {
                moved.push_back(s.steps[size_t(k)].actions[size_t(a)]);
                moved.back().runtimeId = 0; // out of its group: a step of its own
            }
    }
    if (moved.empty()) return;
    if (hasGroup || hasHeader) action = -1; // a group holds steps, not groups
    if (hasHeader) { // not into another one: past it
        folder = 0;
        while (top > 0 && top < int(s.steps.size()) && s.steps[size_t(top)].folder && s.steps[size_t(top)].folder == s.steps[size_t(top - 1)].folder &&
               !movedSteps.count(top))
            ++top;
    }
    if (action >= 0 && (top >= int(s.steps.size()) || movedSteps.count(top) || s.steps[size_t(top)].actions.empty())) return;
    if (action >= 0 && s.steps[size_t(top)].actions.size() - movedActions.size() + moved.size() > 64) return;
    const quint64 played = [&] {
        const int pos = m_engine->sequencer().position();
        return pos >= 0 && pos < int(s.steps.size()) ? s.steps[size_t(pos)].runtimeId : quint64(0);
    }();
    if (action >= 0) // in a group: its GO and what comes next are the group's
        for (Sequencer::Step &step : moved) {
            step.runtimeId = 0;
            step.next = Sequencer::StepContinue::Wait;
            step.at = -1;
            step.folder = 0;
        }
    else if (!hasHeader)
        for (Sequencer::Step &step : moved) step.folder = folder;
    // The list built again, with what moved at its place
    std::vector<Sequencer::Step> out;
    int newStep = -1, newAction = -1;
    auto putTop = [&] {
        newStep = int(out.size());
        newAction = -1;
        out.insert(out.end(), moved.begin(), moved.end());
    };
    for (int k = 0; k < int(s.steps.size()); ++k) {
        if (top == k && action < 0) putTop();
        if (movedSteps.count(k)) continue;
        Sequencer::Step copy = s.steps[size_t(k)];
        const bool wasGroup = !copy.actions.empty();
        copy.actions.clear();
        const int count = int(s.steps[size_t(k)].actions.size());
        for (int a = 0; a <= count; ++a) {
            if (top == k && action == a) {
                newStep = int(out.size());
                newAction = int(copy.actions.size());
                copy.actions.insert(copy.actions.end(), moved.begin(), moved.end());
                copy.open = true;
            }
            if (a < count && !movedActions.count({k, a})) copy.actions.push_back(s.steps[size_t(k)].actions[size_t(a)]);
        }
        if (wasGroup && copy.actions.empty()) continue; // its last step left
        out.push_back(copy);
    }
    if (top >= int(s.steps.size()) && action < 0) putTop();
    const auto same = [](const std::vector<Sequencer::Step> &x, const std::vector<Sequencer::Step> &y) {
        if (x.size() != y.size()) return false;
        for (size_t k = 0; k < x.size(); ++k)
            if (Sequencer::stepToJson(x[k]) != Sequencer::stepToJson(y[k])) return false;
        return true;
    };
    if (same(out, s.steps)) return;
    s.steps = std::move(out);
    normalizeFolders(s);
    applySequence(cur, s);
    const Sequencer::Sequence now = m_engine->sequencer().sequence(cur);
    for (int k = 0; played && k < int(now.steps.size()); ++k)
        if (now.steps[size_t(k)].runtimeId == played) m_engine->sequencer().setPosition(k);
    m_steps->setCurrentCell(rowOf(newStep, hasHeader ? kHeaderRow : newAction), ColSnapshot);
    emit edited();
}

void SequenceWindow::dropPlace(int y, int *top, int *action, quint64 *folder) const
{
    quint64 ignored;
    if (!folder) folder = &ignored;
    *folder = 0;
    const int row = m_steps->rowAt(y);
    if (row < 0) { // under the last row
        *top = int(m_engine->sequencer().sequence(m_engine->sequencer().current()).steps.size());
        *action = -1;
        return;
    }
    const QRect cell = m_steps->visualItemRect(m_steps->item(row, ColSnapshot));
    const bool upper = y < cell.center().y();
    const int step = stepOfRow(row), child = actionOfRow(row);
    const Sequencer::Sequence s = m_engine->sequencer().sequence(m_engine->sequencer().current());
    const quint64 stepFolder = step < int(s.steps.size()) ? s.steps[size_t(step)].folder : 0;
    if (child == kHeaderRow) { // above it, or into it: at its start, at its end when it is folded
        int first, last;
        folderRange(s, step, &first, &last);
        const bool closed = std::count(s.closedFolders.begin(), s.closedFolders.end(), stepFolder) > 0;
        *top = upper ? first : closed ? last + 1 : first;
        *action = -1;
        *folder = upper ? 0 : stepFolder;
        return;
    }
    if (child >= 0) { // beside a step of a group, in the group
        *top = step;
        *action = child + (upper ? 0 : 1);
        return;
    }
    const bool group = m_steps->item(row, ColSnapshot)->data(Qt::UserRole).toInt() == 1;
    const bool open = group && rowOf(step, 0) >= 0;
    if (group && !upper) { // on a group: at its start when it is open, at its end when it is folded
        *top = step;
        *action = open ? 0 : int(m_engine->sequencer().sequence(m_engine->sequencer().current()).steps[size_t(step)].actions.size());
        return;
    }
    *top = step + (upper ? 0 : 1);
    *action = -1;
    *folder = stepFolder; // beside a step of a group played one GO at a time: in it
}

// A snapshot or a timeline dragged from its list: onto a step, it becomes what the step does (a timeline: Play); below
// the last one, a new step
bool SequenceWindow::eventFilter(QObject *o, QEvent *e)
{
    if (o != m_steps->viewport()) return QWidget::eventFilter(o, e);
    if (e->type() == QEvent::DragLeave) m_dropLine->hide();
    if (e->type() == QEvent::Drop) m_dropLine->hide();
    auto rowsDragged = [this](const QDropEvent *d) { return d->source() == m_steps; };
    if ((e->type() == QEvent::DragEnter || e->type() == QEvent::DragMove) && rowsDragged(static_cast<QDragMoveEvent *>(e))) {
        auto *d = static_cast<QDragMoveEvent *>(e);
        const int y = d->position().toPoint().y();
        int top, action;
        quint64 folder;
        dropPlace(y, &top, &action, &folder);
        // The line where they would go: indented inside a group
        const int over = m_steps->rowAt(y);
        const int lineY = over < 0 ? m_steps->rowViewportPosition(m_steps->rowCount() - 1) + m_steps->rowHeight(m_steps->rowCount() - 1)
                                   : m_steps->rowViewportPosition(over) + (y < m_steps->rowViewportPosition(over) + m_steps->rowHeight(over) / 2 ? 0 : m_steps->rowHeight(over));
        const int indent = action >= 0 || folder ? m_steps->columnViewportPosition(ColSnapshot) + 24 : 0;
        m_dropLine->setGeometry(indent, lineY - 1, m_steps->viewport()->width() - indent, 2);
        m_dropLine->show();
        m_dropLine->raise();
        d->setDropAction(Qt::CopyAction); // the list is built again from the sequence: nothing leaves here
        d->accept();
        return true;
    }
    if (e->type() == QEvent::Drop && rowsDragged(static_cast<QDropEvent *>(e))) {
        auto *d = static_cast<QDropEvent *>(e);
        int top, action;
        quint64 folder;
        dropPlace(d->position().toPoint().y(), &top, &action, &folder);
        const QList<int> rows = selectedSteps();
        d->setDropAction(Qt::CopyAction);
        d->accept();
        QMetaObject::invokeMethod(this, [this, rows, top, action, folder] { moveRowsTo(rows, top, action, folder); }, Qt::QueuedConnection);
        return true;
    }
    if (e->type() == QEvent::DragEnter || e->type() == QEvent::DragMove) {
        auto *d = static_cast<QDragMoveEvent *>(e);
        if (!d->mimeData()->hasFormat(kSnapshotMime) && !d->mimeData()->hasFormat(kTimelineMime)) return false;
        d->acceptProposedAction();
        return true;
    }
    if (e->type() == QEvent::Drop) {
        auto *d = static_cast<QDropEvent *>(e);
        const bool timeline = d->mimeData()->hasFormat(kTimelineMime);
        if (!d->mimeData()->hasFormat(kSnapshotMime) && !timeline) return false;
        const quint64 id = d->mimeData()->data(timeline ? kTimelineMime : kSnapshotMime).toULongLong();
        const int row = m_steps->rowAt(d->position().toPoint().y());
        if (row >= 0) timeline ? setStepTimeline(row, id) : setStepSnapshot(row, id);
        else timeline ? addStep(-1, 0, id) : addStep(-1, id);
        d->acceptProposedAction();
        return true;
    }
    return QWidget::eventFilter(o, e);
}


void SequenceWindow::refreshTransport()
{
    // Another sequence selected (the menu, OSC): its steps
    if (m_engine->sequencer().sequenceId(m_engine->sequencer().current()) != m_shownSequence) {
        refresh();
        return;
    }
    const int previous = m_position;
    const int pos = m_engine->sequencer().position();
    const auto s = m_engine->sequencer().sequence(m_engine->sequencer().current());
    if (pos >= 0 && pos < int(s.steps.size()))
        m_status->setText(QStringLiteral("Played: <b style='color:%1'>%2. %3</b>%4")
                              .arg((m_modified ? QColor(QString::fromLatin1(kChanged)) : kLive).name())
                              .arg(pos + 1)
                              .arg(stepTarget(m_engine, s.steps[size_t(pos)], false).toHtmlEscaped())
                              .arg(m_modified ? QStringLiteral(" <span style='color:%1'>— changed since it was played</span>")
                                                    .arg(QLatin1String(kChanged))
                                              : QString()));
    else
        m_status->setText(QStringLiteral("<span style='color:#8a8a90'>No step played yet</span>"));
    // Update only the old/new cue's styling. Item identity and any active text editor survive GO.
    const QSignalBlocker blocker(m_steps);
    for (int step : {previous, pos}) {
        const int row = rowOf(step);
        if (row < 0 || step >= int(s.steps.size()) || actionOfRow(row) == kHeaderRow) continue;
        const bool current = step == pos;
        auto *number = m_steps->item(row, ColNum);
        if (number) number->setText(current ? QStringLiteral("▶ %1").arg(step+1) : QString::number(step+1));
        for (int column : {ColNum, ColSnapshot}) if (auto *item = m_steps->item(row,column)) {
            auto f=item->font(); f.setBold(current); item->setFont(f);
            QColor color=palette().color(QPalette::Text);
            if (current) color=m_modified?QColor(QString::fromLatin1(kChanged)):kLive;
            else if (column==ColSnapshot && targetGone(m_engine,s.steps[size_t(step)])) color=QColor(255,120,100);
            else if (column==ColSnapshot && s.steps[size_t(step)].timeline) color=QColor(86,196,255);
            item->setForeground(color);
        }
    }
    pollRuns();
    // The steps follow what plays: the one played and the one GO plays stay in sight, not under an editor
    if (pos != m_followed) {
        m_followed = pos;
        if (pos >= 0 && !m_steps->viewport()->isAncestorOf(QApplication::focusWidget())) {
            for (int step : {m_upcoming, pos})
                if (const int row = rowOf(step); row >= 0) m_steps->scrollTo(m_steps->model()->index(row, ColSnapshot));
        }
    }
}

namespace {
constexpr auto kStepsMime = "application/x-nyxmap-sequence-steps";
class SequenceDefinitionEdit : public QUndoCommand {
    Engine *engine;
    quint64 id;
    Sequencer::Sequence before, after;
    bool first = true;

public:
    SequenceDefinitionEdit(Engine *e, quint64 owner, Sequencer::Sequence b, Sequencer::Sequence a, const QString &text)
        : engine(e), id(owner), before(std::move(b)), after(std::move(a))
    {
        setText(text);
    }
    void undo() override { engine->sequencer().setSequence(engine->sequencer().indexOfSequence(id), before); }
    void redo() override
    {
        if (first)
            first = false;
        else
            engine->sequencer().setSequence(engine->sequencer().indexOfSequence(id), after);
    }
};
} // namespace
void SequenceWindow::applySequence(int index, const Sequencer::Sequence &sequence, const QString &text)
{
    auto &sequencer = m_engine->sequencer();
    const auto id = sequencer.sequenceId(index);
    if (!id)
        return;
    const auto before = sequencer.sequence(index);
    sequencer.setSequence(index, sequence);
    if (m_undo)
        m_undo->push(new SequenceDefinitionEdit(m_engine, id, before, sequencer.sequence(index), text));
}
QList<int> SequenceWindow::selectedSteps() const
{
    QList<int> rows;
    for (const auto &index : m_steps->selectionModel()->selectedRows())
        rows << index.row();
    if (rows.isEmpty() && m_steps->currentRow() >= 0)
        rows << m_steps->currentRow();
    std::sort(rows.begin(), rows.end());
    return rows;
}
void SequenceWindow::copySteps(bool cut)
{
    auto sequence = m_engine->sequencer().sequence(m_engine->sequencer().current());
    QJsonArray steps;
    const auto rows = selectedSteps();
    for (int row : rows) {
        if (actionOfRow(row) >= 0 && rows.contains(rowOf(stepOfRow(row)))) continue; // comes with its group
        if (actionOfRow(row) == kHeaderRow) {
            int first, last;
            folderRange(sequence, stepOfRow(row), &first, &last);
            for (int k = first; k <= last; ++k) steps.append(Sequencer::stepToJson(sequence.steps[size_t(k)]));
            continue;
        }
        if (const Sequencer::Step *st = stepAt(sequence, row)) steps.append(Sequencer::stepToJson(*st));
    }
    if (steps.isEmpty())
        return;
    auto *mime = new QMimeData;
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"steps", steps}}).toJson(QJsonDocument::Compact);
    mime->setData(kStepsMime, bytes);
    mime->setText(QString::fromUtf8(bytes));
    QApplication::clipboard()->setMimeData(mime);
    if (cut)
        removeSteps();
}
void SequenceWindow::pasteSteps()
{
    const auto bytes = QApplication::clipboard()->mimeData()->data(kStepsMime);
    if (bytes.size() > 4 * 1024 * 1024)
        return;
    const auto object = QJsonDocument::fromJson(bytes).object();
    if (object.value("version").toInt() != 1 || !object.value("steps").isArray())
        return;
    const auto steps = object.value("steps").toArray();
    if (steps.isEmpty() || steps.size() > 1024)
        return;
    const int index = m_engine->sequencer().current();
    if (index < 0)
        return;
    auto sequence = m_engine->sequencer().sequence(index);
    const int row = m_steps->currentRow();
    int at = std::clamp(stepOfRow(row) + 1, 0, int(sequence.steps.size()));
    if (actionOfRow(row) == kHeaderRow) { // after the group
        int first, last;
        folderRange(sequence, stepOfRow(row), &first, &last);
        at = last + 1;
    }
    std::vector<Sequencer::Step> pasted;
    for (const auto &value : steps) {
        if (!value.isObject())
            return;
        pasted.push_back(Sequencer::stepFromJson(value.toObject()));
    }
    const quint64 pastedFolder = actionOfRow(row) == -1 && stepOfRow(row) >= 0 ? sequence.steps[size_t(stepOfRow(row))].folder : 0;
    for (auto &st : pasted) st.folder = pastedFolder; // in the group it is pasted in, or none
    const bool groups = std::any_of(pasted.begin(), pasted.end(), [](const Sequencer::Step &st) { return !st.actions.empty(); });
    if (actionOfRow(row) >= 0 && !groups) { // beside a step of a group: in the group
        auto &actions = sequence.steps[size_t(stepOfRow(row))].actions;
        if (actions.size() + pasted.size() > 64) return;
        const int inside = actionOfRow(row) + 1, step = stepOfRow(row);
        actions.insert(actions.begin() + inside, pasted.begin(), pasted.end());
        applySequence(index, sequence, QStringLiteral("Paste Sequence Steps"));
        m_steps->setCurrentCell(rowOf(step, inside), ColSnapshot);
        emit edited();
        return;
    }
    sequence.steps.insert(sequence.steps.begin() + at, pasted.begin(), pasted.end());
    applySequence(index, sequence, QStringLiteral("Paste Sequence Steps"));
    m_steps->setCurrentCell(rowOf(at), ColSnapshot);
    emit edited();
}
void SequenceWindow::duplicateSteps()
{
    const int index = m_engine->sequencer().current();
    if (index < 0)
        return;
    auto sequence = m_engine->sequencer().sequence(index);
    const auto rows = selectedSteps();
    if (rows.empty())
        return;
    // Steps of one group: beside them, in it
    const int group = stepOfRow(rows.front());
    const bool inGroup = std::all_of(rows.begin(), rows.end(), [&](int row) { return actionOfRow(row) >= 0 && stepOfRow(row) == group; });
    std::vector<Sequencer::Step> copies;
    int lastStep = -1;
    for (int row : rows) {
        if (!inGroup && actionOfRow(row) >= 0) continue;
        if (actionOfRow(row) == kHeaderRow) { // another group beside it
            int first, last;
            folderRange(sequence, stepOfRow(row), &first, &last);
            const quint64 folder = newFolderId(sequence);
            for (int k = first; k <= last; ++k) {
                auto copy = sequence.steps[size_t(k)];
                copy.runtimeId = 0;
                copy.at = -1;
                copy.folder = folder;
                copies.push_back(copy);
            }
            lastStep = last;
            continue;
        }
        if (const Sequencer::Step *st = stepAt(sequence, row)) {
            auto copy = *st;
            copy.runtimeId = 0;
            copy.at = -1;
            copies.push_back(copy);
            lastStep = stepOfRow(row);
        }
    }
    if (copies.empty()) return;
    if (inGroup) {
        auto &actions = sequence.steps[size_t(group)].actions;
        if (actions.size() + copies.size() > 64) return;
        const int inside = actionOfRow(rows.back()) + 1;
        actions.insert(actions.begin() + inside, copies.begin(), copies.end());
        applySequence(index, sequence, QStringLiteral("Duplicate Sequence Steps"));
        m_steps->setCurrentCell(rowOf(group, inside), ColSnapshot);
        emit edited();
        return;
    }
    const int at = std::min(lastStep + 1, int(sequence.steps.size()));
    sequence.steps.insert(sequence.steps.begin() + at, copies.begin(), copies.end());
    applySequence(index, sequence, QStringLiteral("Duplicate Sequence Steps"));
    m_steps->setCurrentCell(rowOf(at), ColSnapshot);
    emit edited();
}
void SequenceWindow::groupSteps()
{
    const int index = m_engine->sequencer().current();
    if (index < 0)
        return;
    auto sequence = m_engine->sequencer().sequence(index);
    QList<int> rows;
    for (int row : selectedSteps()) {
        if (actionOfRow(row) != -1) return; // a group holds steps, not groups
        rows << stepOfRow(row);
    }
    if (rows.empty()) { // none selected: a new group, a first step in it
        Sequencer::Step fresh;
        fresh.actions.emplace_back();
        sequence.steps.push_back(fresh);
        const int at = int(sequence.steps.size()) - 1;
        applySequence(index, sequence, QStringLiteral("Create Group"));
        m_steps->setCurrentCell(rowOf(at), ColSnapshot);
        emit edited();
        return;
    }
    if (rows.size() == 1 && !sequence.steps[size_t(rows.front())].actions.empty()) return; // already one
    groupRange(rows);
}

void SequenceWindow::groupRange(QList<int> rows, bool leaveFolder)
{
    const int index = m_engine->sequencer().current();
    auto sequence = m_engine->sequencer().sequence(index);
    // A group replaces a contiguous cue range; silently moving unrelated cues would alter the show.
    if (rows.back() - rows.front() + 1 != rows.size())
        return;
    auto group = sequence.steps[size_t(rows.front())];
    group.open = true;
    if (leaveFolder) group.folder = 0;
    group.snapshot = group.timeline = group.preset = group.sequence = 0;
    group.actions.clear();
    for (int row : rows) {
        auto step = sequence.steps[size_t(row)];
        if (step.actions.empty())
            group.actions.push_back(step);
        else
            group.actions.insert(group.actions.end(), step.actions.begin(), step.actions.end());
    }
    if (group.actions.size() > 64)
        return;
    for (auto &action : group.actions) { // each keeps its waits, but for what the group itself takes on
        action.runtimeId = 0;
        action.next = Sequencer::StepContinue::Wait;
        action.at = -1;
        action.folder = 0;
    }
    group.actions.front().preWait = 0;
    group.actions.front().description.clear();
    group.actions.back().postWait = 0;
    group.postWait = sequence.steps[size_t(rows.back())].postWait;
    group.next = sequence.steps[size_t(rows.back())].next;
    sequence.steps.erase(sequence.steps.begin() + rows.front(), sequence.steps.begin() + rows.back() + 1);
    sequence.steps.insert(sequence.steps.begin() + rows.front(), group);
    applySequence(index, sequence, QStringLiteral("Create Group"));
    m_steps->setCurrentCell(rowOf(rows.front()), ColSnapshot);
    emit edited();
}
void SequenceWindow::ungroupStep()
{
    const int index = m_engine->sequencer().current(), row = stepOfRow(m_steps->currentRow());
    auto sequence = m_engine->sequencer().sequence(index);
    if (actionOfRow(m_steps->currentRow()) == kHeaderRow) { // steps like any other already
        int first, last;
        folderRange(sequence, row, &first, &last);
        for (int k = first; k <= last; ++k) sequence.steps[size_t(k)].folder = 0;
        normalizeFolders(sequence);
        applySequence(index, sequence, QStringLiteral("Ungroup"));
        m_steps->setCurrentCell(rowOf(first), ColSnapshot);
        emit edited();
        return;
    }
    if (row < 0 || row >= int(sequence.steps.size()))
        return;
    const auto group = sequence.steps[size_t(row)];
    if (group.actions.empty())
        return;
    auto actions = group.actions;
    for (auto &action : actions) {
        action.runtimeId = 0;
        action.next = Sequencer::StepContinue::AutoFollow;
        action.folder = group.folder;
    }
    actions.front().runtimeId = group.runtimeId;
    actions.front().preWait += group.preWait;
    actions.front().at = group.at;
    if (!group.description.isEmpty()) actions.front().description = group.description;
    actions.back().postWait += group.postWait;
    actions.back().next = group.next;
    sequence.steps.erase(sequence.steps.begin() + row);
    sequence.steps.insert(sequence.steps.begin() + row, actions.begin(), actions.end());
    applySequence(index, sequence, QStringLiteral("Ungroup"));
    m_steps->setCurrentCell(rowOf(row), ColSnapshot);
    emit edited();
}
