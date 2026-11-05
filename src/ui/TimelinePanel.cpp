#include "TimelinePanel.h"
#include "Commands.h"
#include "Engine.h"
#include "Widgets.h"

#include <QApplication>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QPointer>
#include <QHeaderView>
#include <QStackedWidget>
#include <QTimer>
#include "TimelineActivity.h"
#include <QUndoStack>
#include <QVBoxLayout>

static const QColor kPlaying(76, 217, 100);

void TimelineWindow::fillParameterMenu(QMenu *menu, Engine *engine, QUndoStack *undo, quint64 layer,
                                      const QStringList &paths, QWidget *anchor)
{
    QStringList targets;
    for (const auto &path : paths) {
        ParamInfo info;
        if (engine->parameterInfo(layer, path, &info) && info.animatable) targets << path;
    }
    if (targets.isEmpty()) return;
    QMenu *animate = nullptr;
    for (auto *action : menu->actions()) if (action->text() == QLatin1String("Animate")) animate = action->menu();
    if (!animate) animate = menu->addMenu(QStringLiteral("Animate"));
    fillValueMenu(animate->addMenu(QStringLiteral("Timeline")), engine, undo, layer, targets, anchor);
}

void TimelineWindow::fillValueMenu(QMenu *menu, Engine *engine, QUndoStack *undo, quint64 layer,
                                  const QStringList &paths, QWidget *anchor)
{
    const QPointer<QWidget> widget = anchor;
    for (int i = 0; i < engine->animationCount(); ++i) {
        const auto a = engine->animation(i);
        if (std::none_of(a.tracks.begin(), a.tracks.end(), [&](const auto &t) {
                return t.layer == layer &&
                       std::any_of(paths.begin(), paths.end(), [&](const auto &p) { return t.drives(p); });
            }))
            continue;
        menu->addAction(QStringLiteral("Go to timeline driver · %1").arg(a.name), menu, [widget, id = a.id] {
            QWidget *root = widget;
            while (root && root->parentWidget())
                root = root->parentWidget();
            if (auto *window = root ? root->findChild<TimelineWindow *>() : nullptr) {
                window->selectTimeline(id);
                emit window->showRequested();
            }
        });
    }
    auto add = [engine, undo, layer, paths, widget](quint64 id) {
        if (layer && engine->isLocked(engine->indexOfId(layer))) return;
        const int index = engine->indexOfAnimation(id);
        if (id && index < 0) return; // a target deleted while the menu was open
        const Animation before = index >= 0 ? engine->animation(index) : Animation();
        Animation after = before;
        if (!id) after.name = QStringLiteral("Timeline %1").arg(engine->animationCount() + 1);
        bool changed = false;
        const auto numbers = engine->animatableParams(layer);
        const auto colors = engine->animatableColors(layer);
        const auto points = engine->animatablePoints(layer);
        for (const auto &path : paths) {
            const bool color = std::any_of(colors.begin(), colors.end(), [&](const auto &p) { return p.path == path; });
            const bool point = std::any_of(points.begin(), points.end(), [&](const auto &p) { return p.path == path; });
            if (!color && !point && std::none_of(numbers.begin(), numbers.end(), [&](const auto &p) { return p.path == path; })) continue;
            if (std::any_of(after.tracks.begin(), after.tracks.end(), [&](const AnimTrack &t) {
                return t.layer == layer && t.param == path && t.color == color && t.isPoint() == point;
            })) continue;
            const auto created = engine->makeLayerAnim(layer, path, -1);
            AnimTrack track = created.tracks.front();
            for (auto &key : track.keys) key.t *= after.duration / created.duration;
            for (auto &axis : track.axes) for (auto &key : axis.keys) key.t *= after.duration / created.duration;
            if (!track.isPoint()) for (auto &existing : after.tracks)
                if (existing.layer == layer && existing.isPoint())
                    for (auto &axis : existing.axes) if (axis.param == path) axis.enabled = false;
            after.tracks.erase(std::remove_if(after.tracks.begin(), after.tracks.end(), [&](const AnimTrack &t) {
                const auto driven = track.drivenParams();
                return t.layer == layer && std::any_of(driven.begin(), driven.end(), [&](const auto &p) { return t.drives(p); });
            }), after.tracks.end());
            after.tracks.push_back(track);
            changed = true;
        }
        if (!id && !changed) return;
        if (!id) {
            auto *command = new cmd::AddAnimation(engine, after, engine->animationCount(), QStringLiteral("New Timeline from Parameter"));
            if (undo) undo->push(command); else command->redo();
            id = command->animationId();
            if (!undo) delete command;
        } else if (changed) {
            if (undo) undo->push(new cmd::SetAnimation(engine, id, before, after, QStringLiteral("Add Parameter to Timeline")));
            else engine->setAnimation(index, after);
        }
        QWidget *root = widget.data();
        while (root && root->parentWidget()) root = root->parentWidget();
        if (auto *window = root ? root->findChild<TimelineWindow *>() : nullptr) {
            window->selectTimeline(id);
            emit window->showRequested();
        }
    };
    menu->addAction(QStringLiteral("New Timeline"), menu, [add] { add(0); });
    if (engine->animationCount()) menu->addSeparator();
    for (int i = 0; i < engine->animationCount(); ++i) {
        const auto timeline = engine->animation(i);
        menu->addAction(timeline.name, menu, [add, id = timeline.id] { add(id); });
    }
}

// ---------------------------------------------------------------------------
// TimelineList

TimelineList::TimelineList()
    : LibraryList(TimelineRole, GroupRole, "application/x-nyxmap-timeline-node", {QString::fromLatin1(kTimelineMime)})
{
    carry = [](QMimeData *m, QTreeWidgetItem *it) { // onto a step of a sequence
        m->setData(kTimelineMime, QByteArray::number(it->data(0, TimelineRole).toULongLong()));
        m->setText(it->text(1));
    };
}

// ---------------------------------------------------------------------------
// A timeline's track: on or off, the layer (or the composition) and the number it drives, then the common settings

class TimelineTrackRow : public TrackRow
{
public:
    TimelineTrackRow(TimelineEditor *editor, int index) : TrackRow(editor, index), m_tl(editor) {}
    void fill() override;

protected:
    void buildHeader(QGridLayout *g) override;

private:
    void fillParams(); // the numbers of its layer, in the menu
    void selectParam(const QString &path);
    void selectColor(const QString &path);
    void selectPoint(const QString &path);
    void selectEvent(const QString &path);
    TimelineEditor *m_tl;
    FlagBox *m_on = nullptr;
    QComboBox *m_layer = nullptr;
    QPushButton *m_param = nullptr; // its menu: the categories, each one a sub-menu of its numbers
};

void TimelineTrackRow::buildHeader(QGridLayout *g)
{
    m_on = new FlagBox;
    m_on->setToolTip(QStringLiteral("On: the track drives its parameter while the timeline plays"));
    m_layer = new QComboBox;
    m_layer->setToolTip(QStringLiteral("The layer driven (or the composition)"));
    auto *del = new QPushButton(QStringLiteral("✕"));
    del->setFixedWidth(26);
    del->setToolTip(QStringLiteral("Delete the track"));
    m_param = new QPushButton;
    m_param->setObjectName(QStringLiteral("timelineParameter"));
    m_param->setToolTip(QStringLiteral("Choose a point (XY), color (RGB), individual numeric parameter, or an event (an action fired at each flag)"));
    m_param->setMenu(new QMenu(m_param));
    m_param->setStyleSheet(QStringLiteral("QPushButton { text-align:left; padding-left:6px; }"));
    g->addWidget(m_on, 0, 0);
    g->addWidget(m_layer, 0, 1, 1, 2);
    g->addWidget(del, 0, 3);
    g->addWidget(m_param, 1, 0, 1, 4);

    connect(m_on, &QCheckBox::toggled, this, [this](bool on) {
        if (m_filling) return;
        track().enabled = on;
        changed();
    });
    connect(m_layer, qOverload<int>(&QComboBox::activated), this, [this](int) {
        track().layer = m_layer->currentData().toULongLong();
        const auto &params = m_ed->paramsOf(track().layer);
        // Its first number, from where it is
        track().param = params.empty() ? QString() : params.front().path;
        track().color = false;
        track().event = false;
        track().axes.clear();
        track().keys = {AnimKey{0, currentValue(), 3}};
        fillParams();
        fill();
        changed();
    });
    connect(del, &QPushButton::clicked, this, [this] {
        QTimer::singleShot(0, m_tl, [tl = m_tl, i = m_i] { tl->deleteTrack(i); }); // not from inside this row
    });
    // The layers (top first), then the composition
    m_layer->addItem(QStringLiteral("Composition"), QVariant::fromValue<quint64>(0));
    {
        Engine::Lock lk(&m_e->mutex());
        for (int i = 0; i < m_e->layerCount(); ++i) {
            const Layer *l = m_e->layer(i);
            m_layer->addItem((l->isViewport ? QStringLiteral("▣ ") : l->isGroup ? QStringLiteral("▤ ") : QString()) + l->name,
                             QVariant::fromValue<quint64>(l->id));
        }
    }
    fillParams();
}

void TimelineTrackRow::fillParams()
{
    QMenu *menu = m_param->menu();
    menu->clear();
    const auto events = m_e->triggerParams(track().layer);
    if (!events.empty()) {
        auto *eventMenu = menu->addMenu(QStringLiteral("Events (actions)"));
        addParamMenu(eventMenu, events, [this](QMenu *into, const Engine::AnimParam &p, const QString &text) {
            auto *a = into->addAction(text, this, [this, path = p.path] { selectEvent(path); });
            a->setCheckable(true);
            a->setChecked(track().event && p.path == track().param);
        });
    }
    // Flags that set a switch, a choice or a number a curve does not drive (Play, Blend Mode, Seek To…)
    const auto settable = m_e->settableParams(track().layer);
    if (!settable.empty()) {
        auto *setMenu = menu->addMenu(QStringLiteral("Events (set a value)"));
        addParamMenu(setMenu, settable, [this](QMenu *into, const Engine::AnimParam &p, const QString &text) {
            auto *a = into->addAction(text, this, [this, path = p.path] { selectEvent(path); });
            a->setCheckable(true);
            a->setChecked(track().event && p.path == track().param);
        });
    }
    const auto points = m_e->animatablePoints(track().layer);
    if (!points.empty()) {
        auto *pointMenu = menu->addMenu(QStringLiteral("Points (XY)"));
        addParamMenu(pointMenu, points, [this](QMenu *into, const Engine::AnimParam &p, const QString &text) {
            into->addAction(text, this, [this, path = p.path] { selectPoint(path); });
        });
    }
    const auto colors = m_e->animatableColors(track().layer);
    if (!colors.empty()) {
        auto *colorMenu = menu->addMenu(QStringLiteral("Colors (RGB)"));
        addParamMenu(colorMenu, colors, [this](QMenu *into, const Engine::AnimParam &p, const QString &text) {
            auto *a = into->addAction(text, this, [this, path = p.path] { selectColor(path); });
            a->setCheckable(true);
            a->setChecked(track().color && p.path == track().param);
        });
        menu->addSeparator();
    }
    addParamMenu(menu, animationMenuParams(m_e, track().layer), [this, colors, points](QMenu *into, const Engine::AnimParam &p, const QString &text) {
        const bool color = std::any_of(colors.begin(), colors.end(), [&](const auto &c) { return c.path == p.path; });
        const bool point = std::any_of(points.begin(), points.end(), [&](const auto &c) { return c.path == p.path; });
        QAction *a = into->addAction(text, this, [this, path = p.path, color, point] {
            if (point) selectPoint(path); else if (color) selectColor(path); else selectParam(path);
        });
        a->setCheckable(true);
        a->setChecked(color == track().color && point == track().isPoint() && p.path == track().param);
    });
}

void TimelineTrackRow::selectParam(const QString &path)
{
    track().color = false;
    track().event = false;
    track().axes.clear();
    track().param = path;
    track().keys = {AnimKey{0, currentValue(), 3}};
    m_e->waveAround(track().layer, path, &track().center, &track().amplitude);
    fillParams(); // the check mark
    fill();
    changed();
}

void TimelineTrackRow::selectColor(const QString &path)
{
    auto &t = track();
    t.param = path;
    t.color = true;
    t.event = false;
    t.axes.clear();
    t.oscillator = false;
    AnimKey key{0, 0, 0};
    m_e->animColorValue(t.layer, path, &key.rgb);
    t.keys = {key};
    fillParams();
    fill();
    changed();
}

void TimelineTrackRow::selectPoint(const QString &path)
{
    const bool enabled = track().enabled;
    const auto created = m_e->makeLayerAnim(track().layer, path, -1);
    track() = created.tracks.front();
    track().enabled = enabled;
    for (auto &axis : track().axes) for (auto &key : axis.keys) key.t *= m_ed->valueDuration() / created.duration;
    fillParams();
    fill();
    changed();
}

void TimelineTrackRow::selectEvent(const QString &path)
{
    auto &t = track();
    const bool wasEvent = t.event;
    ParamInfo before, after;
    const bool hadTarget = wasEvent && m_e->parameterInfo(t.layer, t.param, &before);
    m_e->parameterInfo(t.layer, path, &after);
    t.param = path;
    t.event = true;
    t.color = t.oscillator = false;
    t.axes.clear();
    // What a flag sets at first: a switch on, the first choice, nothing else (a number: 0, as the lane adds one)
    const double first = after.type == ParamInfo::Type::Bool ? 1 : 0;
    if (!wasEvent) t.keys = {AnimKey{0, first, 0}}; // one flag at the start
    else if (!hadTarget || before.type != after.type || before.choices != after.choices) {
        // Another kind of value: the flags keep their times, not values that would mean something else here
        for (size_t k = 0; k < t.keys.size(); ++k)
            t.keys[k].v = after.type == ParamInfo::Type::Bool ? (k % 2 == 0 ? 1 : 0) : 0;
    }
    fillParams();
    fill();
    changed();
}

void TimelineTrackRow::fill()
{
    m_filling = true;
    const AnimTrack &t = track();
    m_on->setChecked(t.enabled);
    int li = m_layer->findData(QVariant::fromValue<quint64>(t.layer));
    if (li < 0) { // the layer is gone: said so, the track is kept
        m_layer->addItem(QStringLiteral("(layer gone)"), QVariant::fromValue<quint64>(t.layer));
        li = m_layer->count() - 1;
    }
    m_layer->setCurrentIndex(li);
    QString label = t.param.isEmpty() ? QStringLiteral("(choose a parameter)") : QStringLiteral("(%1 — not on this layer)").arg(t.param);
    auto events = m_e->triggerParams(t.layer);
    if (t.event) for (const auto &p : m_e->settableParams(t.layer)) events.push_back(p);
    const auto params = t.event ? events : t.isPoint() ? m_e->animatablePoints(t.layer)
                      : t.color ? m_e->animatableColors(t.layer) : m_ed->paramsOf(t.layer);
    for (const Engine::AnimParam &p : params)
        if (p.path == t.param) label = (t.event ? QStringLiteral("⚑ ") : QString()) + p.label;
    m_param->setText(label);
    m_filling = false;
    TrackRow::fill();
}

// ---------------------------------------------------------------------------
// TimelineEditor

TimelineEditor::TimelineEditor(Engine *engine, QUndoStack *undo, QWidget *parent)
    : AnimEditor(engine, undo, Layout::Side, true, parent)
{
    auto *bottom = new QHBoxLayout;
    m_addTrack = new QPushButton(QStringLiteral("+ Track"));
    m_addTrack->setToolTip(QStringLiteral("A track for the layer selected in the layer list"));
    bottom->addWidget(m_addTrack);
    auto *how = new QLabel(QStringLiteral("Color lane: click to add · drag to move · double-click to edit color · right-click for transitions.\n"
                                          "Curve lane: click or draw keys · double-click to delete · right-click for curve and value.\n"
                                          "Event lane: click to add a flag (its action fires as the playhead reaches it) · drag to move · double-click to delete."));
    how->setStyleSheet("color:#8a8a90; font-size:11px;");
    bottom->addWidget(how, 1);
    mainLayout()->addLayout(bottom);

    connect(m_addTrack, &QPushButton::clicked, this, [this] {
        if (!m_has) return;
        AnimTrack t;
        quint64 layer = m_currentLayer;
        if (m_engine->indexOfId(layer) < 0) { // none selected: the first layer that is not a viewport
            layer = 0;
            Engine::Lock lk(&m_engine->mutex());
            for (int i = 0; i < m_engine->layerCount() && !layer; ++i)
                if (!m_engine->layer(i)->isViewport) layer = m_engine->layer(i)->id;
        }
        t.layer = layer;
        t.param = QStringLiteral("opacity");
        double v = 1;
        m_engine->animParamValue(t.layer, t.param, &v);
        t.keys = {AnimKey{0, v, 3}};
        m_edit.tracks.push_back(t);
        commit(QStringLiteral("Add Track"));
        rebuildRows();
        applyView();
    });
    auto refreshTargets = [this] {
        if (QApplication::mouseButtons() & Qt::LeftButton) return;
        forgetParams(); // a layer's numbers (another source, another effect)
        if (isVisible()) rebuildTargets();
        else m_targetsStale = true; // when it is next seen
    };
    connect(m_engine, &Engine::layersChanged, this, refreshTargets);
    connect(m_engine, &Engine::interactionsChanged, this, refreshTargets);
}

// What its rows show of the rest of the project: the names and the numbers of the layers it moves, the interactions. All its
// rows made again cost a lot in a long timeline: only when one of these has changed
QByteArray TimelineEditor::targetsKey() const
{
    QByteArray key;
    QSet<quint64> seen;
    Engine::Lock lk(&m_engine->mutex());
    for (const auto &track : m_edit.tracks) {
        if (seen.contains(track.layer)) continue;
        seen.insert(track.layer);
        key += QByteArray::number(track.layer) + ':';
        if (const Layer *l = m_engine->layer(m_engine->indexOfId(track.layer)))
            key += l->name.toUtf8() + ':' + QByteArray::number(int(l->type)) + ':' + QByteArray::number(qulonglong(l->effects.size())) + ':' +
                   l->sourcePath.toUtf8();
        key += '\n';
    }
    for (const Interaction &it : m_engine->interactions()) key += QByteArray::number(it.id) + it.name.toUtf8() + '\n';
    return key;
}

void TimelineEditor::rebuildTargets()
{
    m_targetsStale = false;
    const QByteArray key = targetsKey();
    if (key == m_targetsKey) return;
    m_targetsKey = key;
    rebuildRows();
    applyView();
}

void TimelineEditor::showEvent(QShowEvent *e)
{
    AnimEditor::showEvent(e);
    if (m_targetsStale) rebuildTargets();
}

void TimelineEditor::setTimeline(quint64 id)
{
    if (id != m_id) forgetView(); // another timeline: seen whole
    m_id = id;
    reload();
}

bool TimelineEditor::fetch(Animation *a) const
{
    const int i = m_engine->indexOfAnimation(m_id);
    if (i < 0) return false;
    *a = m_engine->animation(i);
    return true;
}

void TimelineEditor::store(const Animation &before, const Animation &after, const QString &text, const QString &mergeKey)
{
    Animation a = after;
    a.name = before.name; // its name is the list's (renamed meanwhile)
    if (m_undo) m_undo->push(new cmd::SetAnimation(m_engine, m_id, before, a, text, mergeKey));
    else m_engine->setAnimation(m_engine->indexOfAnimation(m_id), a);
}

void TimelineEditor::control(AnimAction action, double time) { m_engine->controlAnimation(m_id, action, time); }

TrackRow *TimelineEditor::makeRow(int index) { return new TimelineTrackRow(this, index); }

void TimelineEditor::deleteTrack(int index)
{
    if (index < 0 || index >= int(m_edit.tracks.size())) return;
    m_edit.tracks.erase(m_edit.tracks.begin() + index);
    if (index < int(m_laneViews.size())) m_laneViews.erase(m_laneViews.begin() + index);
    commit(QStringLiteral("Delete Track"));
    rebuildRows();
    applyView();
}

// ---------------------------------------------------------------------------
// TimelineWindow

TimelineWindow::TimelineWindow(Engine *engine, QUndoStack *undo, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint), m_engine(engine), m_undo(undo)
{
    setWindowTitle(QStringLiteral("Timelines"));
    resize(1120, 560);
    auto *h = new QHBoxLayout(this);

    // The list
    m_listPanel = new QWidget;
    auto *left = new QVBoxLayout(m_listPanel);
    left->setContentsMargins(0, 0, 0, 0);
    auto *lb = new QHBoxLayout; // as the snapshots' and the presets'
    lb->setSpacing(4);
    lb->addStretch();
    m_overview = new QPushButton(QStringLiteral("Fold all"));
    m_overview->setObjectName(QStringLiteral("timelinesOverview"));
    m_overview->setCheckable(true);
    m_overview->setToolTip(QStringLiteral("Fold all the timelines: each one's run on a line, with its time and its transport. "
                                          "Double-click one to edit it"));
    m_group = new QPushButton(QStringLiteral("Group"));
    m_group->setObjectName(QStringLiteral("timelineGroup"));
    m_group->setToolTip(QStringLiteral("A new group of timelines: drag timelines (and groups) into it"));
    m_add = new QPushButton(QStringLiteral("+"));
    m_add->setObjectName(QStringLiteral("timelineNew"));
    m_add->setToolTip(QStringLiteral("New timeline"));
    symbolButton(m_add);
    m_del = new QPushButton(QStringLiteral("−"));
    m_del->setObjectName(QStringLiteral("timelineDelete"));
    m_del->setToolTip(QStringLiteral("Delete the timeline (the steps that drive it then do nothing)"));
    symbolButton(m_del);
    m_dup = new QPushButton(QStringLiteral("⧉"));
    m_dup->setObjectName(QStringLiteral("timelineDuplicate"));
    m_dup->setToolTip(QStringLiteral("Duplicate timeline"));
    symbolButton(m_dup);
    m_go = new QPushButton(QStringLiteral("PLAY ▶"));
    m_go->setObjectName(QStringLiteral("timelinePlay"));
    m_go->setStyleSheet("QPushButton { font-weight:bold; background:#2f6b3a; color:white; padding:3px 8px; }");
    m_go->setToolTip(QStringLiteral("Play the selected timeline"));
    for (QPushButton *b : {m_overview, m_group, m_add, m_del, m_dup, m_go}) lb->addWidget(b);
    left->addLayout(lb);
    m_list = new TimelineList;
    m_list->setObjectName(QStringLiteral("timelineList"));
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({QStringLiteral("#"), QStringLiteral("Name"), QStringLiteral("Length")});
    m_list->setRootIsDecorated(true);
    m_list->setIndentation(12);
    m_list->setUniformRowHeights(true);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers); // a double-click renames
    m_list->setExpandsOnDoubleClick(false);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->setColumnWidth(0, 54); // room for the groups' indentation
    m_list->setColumnWidth(2, 58);
    m_list->setToolTip(QStringLiteral("Drag a timeline onto a step of a sequence (Play by default: the step can also pause, stop, rewind, "
                                      "seek or change its loop), or into a group\n"
                                      "Double-click or Return: rename · ⌘Return: play · Delete: delete · ⌘D: duplicate · ⌘G: new group"));
    left->addWidget(new LibraryFilter(m_list, QStringLiteral("timelineFilter"), [this] { refreshList(); }));
    left->addWidget(m_list, 1);
    new ListKeys(m_list, {[this] {
                              if (QTreeWidgetItem *it = m_list->currentItem())
                                  m_list->editItem(it, it->data(0, TimelineList::GroupRole).toULongLong() ? 0 : 1);
                          },
                          [this] { m_go->click(); }, [this] { m_del->click(); }, [this] { m_dup->click(); },
                          [this] { m_group->click(); }});
    m_groups = std::make_unique<LibraryGroups>(m_list, QStringLiteral("Timelines"), LibraryGroups::Access{
        [this] { return m_engine->timelineTree(); },
        [this](const LibraryGroups::Tree &before, const LibraryGroups::Tree &after, const QString &text) {
            auto set = [e = m_engine](const LibraryGroups::Tree &tree) { e->setTimelineTree(tree); };
            if (m_undo) m_undo->push(new cmd::SetLibraryTree(set, before, after, text));
            else set(after);
            emit edited();
        },
        [this](const LibraryGroups::Tree &tree) { // a group folded: the list and the editor stay
            m_renaming = true;
            m_engine->setTimelineTree(tree);
            m_renaming = false;
            emit edited();
        },
        [this] { return m_filling; },
        [this] { return m_engine->newSnapshotGroupId(); }});
    connect(m_group, &QPushButton::clicked, this, [this] { m_groups->newGroup(); });
    connect(m_go, &QPushButton::clicked, this, [this] {
        if (m_engine->indexOfAnimation(m_current) >= 0) m_engine->controlAnimation(m_current, AnimAction::Play);
    });
    connect(m_list, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *row = m_list->itemAt(pos);
        if (row) m_list->setCurrentItem(row);
        QMenu menu;
        menu.addAction(QStringLiteral("New Timeline"), m_add, &QPushButton::click);
        menu.addAction(QStringLiteral("New Group"), this, [this] { m_groups->newGroup(); });
        m_groups->fillMenu(menu, row);
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        m_list->editItem(it, it->data(0, TimelineList::GroupRole).toULongLong() ? 0 : 1);
    });
    h->addWidget(m_listPanel, 1);

    // The one selected
    m_editor = new TimelineEditor(engine, undo);
    m_activity = new TimelineActivity(engine);
    m_activity->setCompact(true);
    m_workspace = new QStackedWidget;
    m_workspace->setObjectName(QStringLiteral("timelineWorkspace"));
    m_workspace->addWidget(m_editor);
    m_workspace->addWidget(m_activity);

    m_activity->setSummary(true);
    m_foldedPage = new QWidget;
    auto *foldedLayout = new QVBoxLayout(m_foldedPage);
    auto *expand = new QPushButton(QStringLiteral("Expand timeline"));
    expand->setObjectName(QStringLiteral("timelineExpand"));
    foldedLayout->addWidget(expand, 0, Qt::AlignLeft);
    m_foldedActivity = new TimelineActivity(engine);
    m_foldedActivity->setCompact(true);
    m_foldedActivity->setSummary(true);
    foldedLayout->addWidget(m_foldedActivity, 1);
    m_workspace->addWidget(m_foldedPage);
    auto *fold = new QPushButton(QStringLiteral("Fold timeline"));
    fold->setObjectName(QStringLiteral("timelineFold"));
    if (auto *editorLayout = dynamic_cast<QVBoxLayout *>(m_editor->QWidget::layout()))
        editorLayout->insertWidget(0, fold, 0, Qt::AlignLeft);
    connect(fold, &QPushButton::clicked, this, [this] {
        if (!m_current) return;
        m_foldedActivity->setOnlyTimeline(m_current);
        m_workspace->setCurrentWidget(m_foldedPage);
    });
    connect(expand, &QPushButton::clicked, this, [this] { m_workspace->setCurrentWidget(m_editor); });
    connect(m_foldedActivity, &TimelineActivity::editRequested, this, [this](quint64) { m_workspace->setCurrentWidget(m_editor); });
    h->addWidget(m_workspace, 4);
    connect(m_overview, &QPushButton::toggled, this, [this](bool all) { m_workspace->setCurrentWidget(all ? static_cast<QWidget *>(m_activity) : m_editor); });
    connect(m_activity, &TimelineActivity::editRequested, this, [this](quint64 id) {
        selectTimeline(id);
        m_overview->setChecked(false);
    });
    connect(m_editor, &AnimEditor::edited, this, &TimelineWindow::edited);

    connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
        if (m_filling) return;
        m_current = it ? it->data(0, TimelineList::TimelineRole).toULongLong() : 0;
        showCurrent();
    });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int column) {
        if (m_filling || column != 1) return;
        const int i = m_engine->indexOfAnimation(it->data(0, TimelineList::TimelineRole).toULongLong());
        if (i < 0) return;
        const Animation before = m_engine->animation(i);
        if (before.name == it->text(1)) return;
        Animation a = before;
        a.name = it->text(1);
        m_renaming = true;
        if (m_undo) m_undo->push(new cmd::SetAnimation(m_engine, a.id, before, a, QStringLiteral("Rename Timeline")));
        else m_engine->setAnimation(i, a);
        m_renaming = false;
        emit edited();
    });
    auto add = [this](const Animation &a, int at, const QString &text) {
        auto *c = new cmd::AddAnimation(m_engine, a, at, text);
        if (m_undo) m_undo->push(c);
        else c->redo();
        m_current = c->animationId();
        if (!m_undo) delete c;
        refreshList();
        showCurrent();
        emit edited();
    };
    connect(m_add, &QPushButton::clicked, this, [this, add] {
        Animation a;
        a.name = QStringLiteral("Timeline %1").arg(m_engine->animationCount() + 1);
        add(a, m_engine->animationCount(), QStringLiteral("New Timeline"));
    });
    connect(m_dup, &QPushButton::clicked, this, [this, add] {
        const int i = m_engine->indexOfAnimation(m_current);
        if (i < 0) return;
        Animation a = m_engine->animation(i);
        a.id = 0;
        a.name += QStringLiteral(" copy");
        add(a, i + 1, QStringLiteral("Duplicate Timeline"));
    });
    connect(m_del, &QPushButton::clicked, this, [this] {
        const QTreeWidgetItem *row = m_list->currentItem();
        if (const quint64 group = row ? row->data(0, TimelineList::GroupRole).toULongLong() : 0) {
            m_groups->removeGroup(group);
            return;
        }
        const int i = m_engine->indexOfAnimation(m_current);
        if (i < 0) return;
        if (m_undo) {
            m_undo->push(new cmd::RemoveAnimation(m_engine, i));
        } else {
            m_engine->removeAnimation(i);
        }
        emit edited();
    });
    connect(m_engine, &Engine::animationsChanged, this, [this] {
        if (m_renaming || m_editor->isCommitting()) return; // its own edit: the rows (a key dragged) stay
        refreshList();
        showCurrent();
    });
    auto *timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, &TimelineWindow::pollList);
    timer->start();
    refreshList();
    showCurrent();
}

void TimelineWindow::refreshList()
{
    QTreeWidgetItem *was = m_list->currentItem();
    const quint64 keptGroup = was ? was->data(0, TimelineList::GroupRole).toULongLong() : 0;
    m_filling = true;
    m_list->clear();
    if (!keptGroup && m_engine->indexOfAnimation(m_current) < 0) m_current = m_engine->animationCount() > 0 ? m_engine->animation(0).id : 0;
    QTreeWidgetItem *current = nullptr;
    int number = 0;
    std::function<void(const std::vector<Engine::SnapshotNode> &, QTreeWidgetItem *)> add =
        [&](const std::vector<Engine::SnapshotNode> &nodes, QTreeWidgetItem *parent) {
            for (const Engine::SnapshotNode &node : nodes) {
                QTreeWidgetItem *it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_list);
                if (!node.snapshot) {
                    LibraryGroups::styleGroup(it, node, TimelineList::GroupRole, QStringLiteral("timelines"));
                    add(node.children, it);
                    it->setExpanded(node.open);
                    if (node.group == keptGroup) current = it;
                    continue;
                }
                const Animation a = m_engine->animation(m_engine->indexOfAnimation(node.snapshot));
                it->setText(0, QString::number(++number));
                it->setText(1, a.name);
                it->setText(2, QStringLiteral("%1 s").arg(a.duration, 0, 'f', 1));
                it->setData(0, TimelineList::TimelineRole, QVariant::fromValue<quint64>(a.id));
                it->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
                it->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
                if (a.id == m_current && !keptGroup) current = it;
            }
        };
    add(m_engine->timelineTree(), nullptr);
    if (current) m_list->setCurrentItem(current);
    m_list->applyFilter();
    m_filling = false;
}

void TimelineWindow::showCurrent()
{
    const bool has = m_engine->indexOfAnimation(m_current) >= 0;
    const QTreeWidgetItem *row = m_list->currentItem();
    m_dup->setEnabled(has);
    m_go->setEnabled(has);
    m_del->setEnabled(has || (row && row->data(0, TimelineList::GroupRole).toULongLong()));
    m_editor->setTimeline(has ? m_current : 0);
    if (m_foldedActivity) m_foldedActivity->setOnlyTimeline(has ? m_current : 0);
}

void TimelineWindow::pollList()
{
    if (!isVisible()) return;
    std::vector<Engine::AnimationPlayback> playback;
    if (!m_engine->sampleAnimations(&playback)) return;
    for (QTreeWidgetItemIterator row(m_list); *row; ++row) {
        QTreeWidgetItem *it = *row;
        const quint64 id = it->data(0, TimelineList::TimelineRole).toULongLong();
        if (!id) continue;
        const auto found = std::find_if(playback.begin(), playback.end(), [id](const auto &a) { return a.id == id; });
        if (found == playback.end()) continue;
        const QBrush b = found->state == AnimState::Playing ? QBrush(kPlaying) : QBrush();
        if (it->foreground(1) != b) {
            const QSignalBlocker blocker(m_list); // not a rename
            it->setForeground(1, b);
        }
    }
}


bool TimelineEditor::fetchPlayback(Engine::AnimationPlayback *a) const { return m_engine->sampleAnimation(m_id, 0, {}, a); }


void TimelineWindow::selectTimeline(quint64 id) {
    if (m_engine->indexOfAnimation(id) < 0) return;
    m_current = id;
    for (QTreeWidgetItemIterator it(m_list); *it; ++it)
        if ((*it)->data(0, TimelineList::TimelineRole).toULongLong() == id) m_list->setCurrentItem(*it);
    showCurrent();
}

QWidget *TimelineWindow::workspace() const { return m_workspace; }
