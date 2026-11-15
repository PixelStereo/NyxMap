#include "Naming.h"
#include "FixturePanel.h"
#include "Commands.h"
#include "Engine.h"
#include "FixtureOverlay.h"
#include "FixtureInspector.h"
#include "Gdtf.h"
#include "Widgets.h"

#include <QApplication>
#include <QCheckBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QUuid>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace {
// The same columns as the layers' list: enable, lock, solo, the name with its picture, then the address (where Blend
// is) and the intensity (where Opacity is), and a last one for what the fixture is made of
enum Column { ColOn, ColLock, ColSolo, ColName, ColDmx, ColIntensity, ColCount };

QToolButton *barButton(const QString &text, const QString &tip)
{
    auto *b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    b->setMinimumSize(30, 24);
    return b;
}

// The next address free on an output, after every fixture already patched to it (Art-Net counts universes from 0)
std::pair<int, int> nextFree(const FixturePatch &p, quint64 output, int channels)
{
    const DmxOutput *out = p.findOutput(output);
    int universe = out && out->protocol == DmxOutput::Protocol::Sacn ? 1 : 0, address = 1;
    bool any = false;
    for (const Fixture &f : p.fixtures) {
        const std::optional<FixtureType> t = p.resolvedType(f);
        if (!t || f.output != output) continue;
        for (const DmxSpan &s : fixtureFootprint(*t, f.universe, f.address))
            if (!any || s.universe > universe || (s.universe == universe && s.last + 1 > address)) {
                universe = s.universe;
                address = s.last + 1;
                any = true;
            }
    }
    if (address + std::min(channels, 512) - 1 > 512) {
        ++universe;
        address = 1;
    }
    return {universe, address};
}
} // namespace

namespace {
// A fixture's or a group's intensity: the application's bar, as on the layers
QWidget *makeIntensityBox(SliderField **out)
{
    auto *box = new QWidget;
    auto *hl = new QHBoxLayout(box);
    hl->setContentsMargins(2, 12, 2, 12); // as the layers' opacity
    auto *slider = new SliderField;
    slider->setObjectName(QStringLiteral("fixtureIntensity"));
    slider->setRange(0, 100);
    slider->setDecimals(0);
    slider->setSuffix(QStringLiteral(" %"));
    slider->setSingleStep(1);
    slider->setSnaps({100});
    slider->setCompact(true);
    slider->setNumberOnly(true); // the number alone, as on the layers
    slider->setFocusPolicy(Qt::NoFocus);
    hl->addWidget(slider, 1);
    *out = slider;
    return box;
}
} // namespace

namespace {
constexpr int kMetaRole = Qt::UserRole + 2;    // under a name: what the fixture is
constexpr int kGroupOfRole = Qt::UserRole + 3; // the group a row is (its header) or is in (a member); 0: none
constexpr int kOpenRole = Qt::UserRole + 4;    // a group's header: open
constexpr int kLookWidth = 56;              // the preview's box, as the layers' small picture

// What a fixture's pixels give out now, in a box: a dimmer in black and white (its level the fill, written on it),
// a colour as its swatch, a bar of pixels as one cell each
using Looks = std::map<quint64, std::vector<std::array<float, 3>>>;
void paintLook(QPainter *p, const QRect &box, const Looks &looks, quint64 id)
{
    p->fillRect(box, QColor(18, 18, 22));
    const auto it = looks.find(id);
    if (it != looks.end() && !it->second.empty()) {
        const auto &px = it->second;
        const auto colour = [](const std::array<float, 3> &c) {
            return QColor::fromRgbF(qBound(0.f, c[0], 1.f), qBound(0.f, c[1], 1.f), qBound(0.f, c[2], 1.f));
        };
        const bool grey = px.size() == 1 && std::abs(px[0][0] - px[0][1]) < 0.01f && std::abs(px[0][1] - px[0][2]) < 0.01f;
        if (grey) {
            const float level = qBound(0.f, px[0][0], 1.f);
            p->fillRect(QRect(box.left(), box.top(), int(box.width() * level + 0.5f), box.height()), QColor::fromRgbF(level, level, level));
            p->setPen(level > 0.5f ? QColor(0, 0, 0) : QColor(235, 235, 240));
            p->drawText(box.adjusted(0, 0, -3, 0), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1%").arg(qRound(level * 100)));
        } else if (px.size() == 1) {
            p->fillRect(box, colour(px[0]));
        } else {
            const int n = int(std::min<size_t>(px.size(), 64)); // in DMX order, sampled past 64
            for (int k = 0; k < n; ++k) {
                const size_t src = px.size() <= 64 ? size_t(k) : size_t(k) * px.size() / 64;
                const int x0 = box.left() + k * box.width() / n, x1 = box.left() + (k + 1) * box.width() / n;
                p->fillRect(QRect(x0, box.top(), std::max(1, x1 - x0 - 1), box.height()), colour(px[src]));
            }
        }
    }
    p->setPen(QColor(70, 70, 78));
    p->setBrush(Qt::NoBrush);
    p->drawRect(box.adjusted(0, 0, -1, -1));
}

// The name cell, as the layers': the live box where the layer's picture is, the name in bold and, under it, what it is
class FixtureNameDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    Looks looks; // by fixture id, 0..1 per pixel, as last sent

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &ix) const override
    {
        QStyleOptionViewItem o = opt;
        initStyleOption(&o, ix);
        o.text.clear();
        o.icon = QIcon();
        QStyle *st = o.widget ? o.widget->style() : QApplication::style();
        st->drawControl(QStyle::CE_ItemViewItem, &o, p, o.widget);
        p->save();
        p->setClipRect(opt.rect);
        const bool group = ix.data(Qt::UserRole + 1).toULongLong() != 0;
        const QColor name = ix.data(Qt::ForegroundRole).value<QBrush>().color();
        int x = opt.rect.left() + 8;
        if (group) { // its fold mark, as every list's
            theme::paintFoldArrow(*p, QPointF(x + 7, opt.rect.center().y() + 0.5), ix.data(kOpenRole).toBool());
            x += 20;
        } else if (ix.data(kGroupOfRole).toULongLong()) {
            x += 16; // a member: under its group, as a layer in a group
        }
        if (!group) {
            p->fillRect(QRect(opt.rect.left(), opt.rect.top() + 1, 3, opt.rect.height() - 2), QColor(232, 195, 90)); // the kind's band
            const QRect box(x, opt.rect.top() + 5, kLookWidth, opt.rect.height() - 10);
            paintLook(p, box, looks, ix.data(Qt::UserRole).toULongLong());
            x = box.right() + 9;
        }
        const QRect textBox(x, opt.rect.top(), opt.rect.right() - x - 4, opt.rect.height());
        QFont f = opt.font;
        f.setBold(true);
        QFontMetrics fm(f);
        p->setFont(f);
        p->setPen(name);
        p->drawText(QRect(textBox.left(), textBox.top() + 5, textBox.width(), fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(ix.data(Qt::DisplayRole).toString(), Qt::ElideRight, textBox.width()));
        QFont g = opt.font;
        g.setPointSizeF(std::max(7.0, g.pointSizeF() - 1.5));
        QFontMetrics gm(g);
        p->setFont(g);
        p->setPen(QColor(140, 140, 150));
        p->drawText(QRect(textBox.left(), textBox.top() + 6 + fm.height(), textBox.width(), gm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    gm.elidedText(ix.data(kMetaRole).toString(), Qt::ElideRight, textBox.width()));
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return QSize(160, 44); }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &opt, const QModelIndex &ix) const override
    {
        const bool group = ix.data(Qt::UserRole + 1).toULongLong() != 0;
        const int member = !group && ix.data(kGroupOfRole).toULongLong() ? 16 : 0;
        editor->setGeometry(opt.rect.adjusted(group ? 28 : 8 + member + kLookWidth + 9, 4, -4, -4)); // over the name, not the box
    }
};
// The list: each group a fine line around its header and its fixtures, as the layers' groups
class FixtureGrid : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QTableWidget::paintEvent(e);
        QPainter p(viewport());
        QColor line(232, 214, 170);
        line.setAlpha(170);
        p.setPen(QPen(line, 1));
        p.setBrush(Qt::NoBrush);
        const auto groupOf = [this](int r) { const QTableWidgetItem *it = item(r, ColName); return it ? it->data(kGroupOfRole).toULongLong() : 0; };
        const auto header = [this](int r) { const QTableWidgetItem *it = item(r, ColName); return it && it->data(Qt::UserRole + 1).toULongLong(); };
        for (int r = 0; r < rowCount(); ++r) {
            if (!header(r) || isRowHidden(r)) continue;
            int last = r;
            for (int k = r + 1; k < rowCount() && !header(k) && groupOf(k) == groupOf(r); ++k) last = k;
            const int top = rowViewportPosition(r);
            p.drawRect(1, top, viewport()->width() - 3, rowViewportPosition(last) + rowHeight(last) - top - 1);
        }
    }
};
} // namespace

FixturePanel::FixturePanel(Engine *engine, QUndoStack *undo, QWidget *parent)
    : QWidget(parent), m_engine(engine), m_undo(undo)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(8, 4, 8, 4);
    v->setSpacing(4);
    auto *bar = new QHBoxLayout;
    bar->setSpacing(2);
    m_add = barButton(QStringLiteral("+"), QStringLiteral("New fixture of a type, at the next free address"));
    m_add->setObjectName(QStringLiteral("fixturesAdd"));
    m_add->setPopupMode(QToolButton::InstantPopup);
    m_addMenu = new QMenu(m_add);
    m_add->setMenu(m_addMenu);
    connect(m_addMenu, &QMenu::aboutToShow, this, &FixturePanel::fillAddMenu);
    auto *remove = barButton(QStringLiteral("−"), QStringLiteral("Delete the selected fixtures (Del)"));
    auto *dup = barButton(QStringLiteral("⧉"), QStringLiteral("Duplicate the selected fixtures, at the next free addresses"));
    for (auto *b : {m_add, remove, dup}) bar->addWidget(b);
    bar->addStretch(1);
    v->addLayout(bar);

    m_table = new FixtureGrid(0, ColCount);
    m_table->setObjectName(QStringLiteral("fixturesTable"));
    m_table->setHorizontalHeaderLabels({QString(), QString(), QString(), QStringLiteral("Fixture"), QStringLiteral("DMX"),
                                        QStringLiteral("Int.")});
    m_table->horizontalHeaderItem(ColName)->setToolTip(QStringLiteral("The box shows what it sends now: black and white for a dimmer, its colour, its pixels"));
    m_table->horizontalHeaderItem(ColOn)->setToolTip(QStringLiteral("Enable"));
    m_table->horizontalHeaderItem(ColLock)->setToolTip(QStringLiteral("Locked: not moved in the views"));
    m_table->horizontalHeaderItem(ColSolo)->setToolTip(QStringLiteral("Solo: the other fixtures send black"));
    m_table->horizontalHeaderItem(ColIntensity)->setToolTip(QStringLiteral("A factor on everything the fixture sends: drag the number sideways, double-click to type"));
    m_table->horizontalHeaderItem(ColDmx)->setToolTip(QStringLiteral("Universe · channels"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(44);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setIconSize(QSize(16, 16));
    auto *h = m_table->horizontalHeader();
    h->setHighlightSections(false);
    // The widths of the layers' list, so the two lists line up one under the other; the name takes what is left
    for (auto [col, width] : {std::pair{ColOn, 28}, {ColLock, 26}, {ColSolo, 24}, {ColDmx, 92}, {ColIntensity, 62}}) {
        h->setSectionResizeMode(col, QHeaderView::Fixed);
        m_table->setColumnWidth(col, width);
    }
    h->setSectionResizeMode(ColName, QHeaderView::Stretch);
    h->setStretchLastSection(false);
    h->setMinimumSectionSize(16);
    v->addWidget(m_table, 1);
    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    v->addWidget(m_summary);

    connect(remove, &QToolButton::clicked, this, &FixturePanel::removeFixtures);
    connect(dup, &QToolButton::clicked, this, &FixturePanel::duplicateFixtures);
    auto *del = new QShortcut(QKeySequence::Delete, m_table, nullptr, nullptr, Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, &FixturePanel::removeFixtures);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        if (!m_updating) emit fixtureSelected(selectedFixture());
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (m_updating) return;
        if (const quint64 group = groupAt(it->row())) { // a group's enable or name
            if (it->column() == ColOn) {
                const bool on = it->checkState() == Qt::Checked;
                cmd::editFixtures(m_undo, m_engine, on ? QStringLiteral("Enable Group") : QStringLiteral("Disable Group"),
                                  [&](FixturePatch &p) {
                                      for (FixtureGroup &g : p.groups)
                                          if (g.id == group) g.enabled = on;
                                  });
                emit projectEdited();
            } else if (it->column() == ColName) {
                QString name = it->text().trimmed();
                name.remove(QRegularExpression(QStringLiteral("^[▾▸]\\s*")));
                name = name.trimmed();
                if (!name.isEmpty()) {
                    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Rename Group"), [&](FixturePatch &p) {
                        for (FixtureGroup &g : p.groups)
                            if (g.id == group) g.name = name;
                    });
                    emit projectEdited();
                }
            }
            return;
        }
        const quint64 id = idAt(it->row());
        if (it->column() == ColOn) {
            const bool on = it->checkState() == Qt::Checked;
            cmd::editFixtures(m_undo, m_engine, on ? QStringLiteral("Enable Fixture") : QStringLiteral("Disable Fixture"),
                              [&](FixturePatch &p) {
                                  for (Fixture &f : p.fixtures)
                                      if (f.id == id) f.enabled = on;
                              });
        } else if (it->column() == ColName) {
            const QString name = it->text().trimmed();
            cmd::editFixtures(m_undo, m_engine, QStringLiteral("Rename Fixture"), [&](FixturePatch &p) {
                for (Fixture &f : p.fixtures)
                    if (f.id == id) f.name = name;
            });
        } else {
            return;
        }
        emit projectEdited();
    });
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (const quint64 group = groupAt(row)) { // a group's row: its lock and solo, or fold it
            if (col == ColLock || col == ColSolo) {
                cmd::editFixtures(m_undo, m_engine, col == ColLock ? QStringLiteral("Lock Group") : QStringLiteral("Solo Group"),
                                  [&](FixturePatch &p) {
                                      for (FixtureGroup &g : p.groups)
                                          if (g.id == group) (col == ColLock ? g.locked : g.solo) ^= true;
                                  });
                emit projectEdited();
            } else if (col == ColName || col == ColDmx) {
                cmd::editFixtures(m_undo, m_engine, QStringLiteral("Fold Group"), [&](FixturePatch &p) {
                    for (FixtureGroup &g : p.groups)
                        if (g.id == group) g.open = !g.open;
                });
                emit projectEdited();
            }
            return;
        }
        if (col != ColLock && col != ColSolo) return;
        const quint64 id = idAt(row);
        cmd::editFixtures(m_undo, m_engine, col == ColLock ? QStringLiteral("Lock Fixture") : QStringLiteral("Solo Fixture"),
                          [&](FixturePatch &p) {
                              for (Fixture &f : p.fixtures)
                                  if (f.id == id) (col == ColLock ? f.locked : f.solo) ^= true;
                          });
        emit projectEdited();
    });

    // The list follows the engine; a burst of changes (a fixture dragged in a view) is one refresh
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(60);
    connect(m_refreshTimer, &QTimer::timeout, this, &FixturePanel::refresh);
    connect(m_engine, &Engine::fixturesChanged, m_refreshTimer, qOverload<>(&QTimer::start));
    // The live column follows the frames: repainted while shown, from what the pixels last gave out
    auto *preview = new FixtureNameDelegate(m_table);
    m_table->setItemDelegateForColumn(ColName, preview);
    auto *live = new QTimer(this);
    live->setInterval(33);
    connect(live, &QTimer::timeout, this, [this, preview] {
        if (!m_table->isVisible()) return;
        preview->looks = m_engine->fixtureLooks();
        m_table->viewport()->update();
    });
    live->start();
    refresh();
}

quint64 FixturePanel::idAt(int row) const
{
    const QTableWidgetItem *i = row >= 0 ? m_table->item(row, ColName) : nullptr;
    return i ? i->data(Qt::UserRole).toULongLong() : 0;
}

std::vector<quint64> FixturePanel::selectedIds() const
{
    std::vector<quint64> ids;
    for (const QModelIndex &i : m_table->selectionModel()->selectedRows())
        if (const quint64 id = idAt(i.row())) ids.push_back(id); // a group's row is not a fixture
    return ids;
}

quint64 FixturePanel::groupAt(int row) const
{
    const QTableWidgetItem *i = row >= 0 ? m_table->item(row, ColName) : nullptr;
    return i ? i->data(Qt::UserRole + 1).toULongLong() : 0;
}

// The group a new fixture goes into, and the one Cmd / Ctrl + G puts the selection in: a group's row selected, else the
// group of the current fixture
quint64 FixturePanel::selectedGroup() const
{
    if (m_table->selectionModel())
        for (const QModelIndex &i : m_table->selectionModel()->selectedRows())
            if (const quint64 g = groupAt(i.row())) return g;
    const int row = m_table->currentRow();
    if (const quint64 g = groupAt(row)) return g;
    const quint64 id = idAt(row);
    if (!id) return 0;
    const Fixture *f = m_engine->fixturePatch().findFixture(id);
    return f ? f->group : 0;
}

void FixturePanel::editIntensity(SliderField *slider, double value)
{
    for (int k = 0; k < m_table->rowCount(); ++k)
        if (QWidget *c = m_table->cellWidget(k, ColIntensity); c && c->isAncestorOf(slider)) {
            if (const quint64 group = groupAt(k)) {
                cmd::editFixtures(m_undo, m_engine, QStringLiteral("Group Intensity"), [&](FixturePatch &p) {
                    for (FixtureGroup &g : p.groups)
                        if (g.id == group) g.intensity = value / 100.0;
                }, QStringLiteral("group-intensity-%1").arg(group));
                emit projectEdited();
            } else {
                const quint64 id = idAt(k);
                cmd::editFixtures(m_undo, m_engine, QStringLiteral("Fixture Intensity"), [&](FixturePatch &q) {
                    for (Fixture &x : q.fixtures)
                        if (x.id == id) x.intensity = value / 100.0;
                }, QStringLiteral("fixture-intensity-%1").arg(id));
                emit projectEdited();
            }
            break;
        }
}

// The selected fixtures in a new group (one undo step), named "Group n" with the first free n
void FixturePanel::groupSelected()
{
    const std::vector<quint64> chosen = selectedIds();
    const std::set<quint64> ids(chosen.begin(), chosen.end());
    const quint64 existing = selectedGroup(); // a group selected: the fixtures join it; else a new group
    quint64 made = 0;
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Group Fixtures"), [&](FixturePatch &p) {
        if (existing && p.findGroup(existing)) {
            made = existing;
        } else {
            for (const FixtureGroup &g : p.groups) made = std::max(made, g.id);
            ++made;
            FixtureGroup g;
            g.id = made;
            for (int n = 1;; ++n) {
                g.name = QStringLiteral("Group %1").arg(n);
                if (std::none_of(p.groups.begin(), p.groups.end(), [&](const FixtureGroup &o) { return o.name == g.name; })) break;
            }
            p.groups.push_back(g);
        }
        for (Fixture &f : p.fixtures)
            if (ids.count(f.id)) f.group = made;
    });
    if (made) {
        refresh();
        emit projectEdited();
    }
}

quint64 FixturePanel::selectedFixture() const
{
    const int row = m_table->currentRow();
    if (row >= 0 && m_table->selectionModel()->isRowSelected(row, {})) return idAt(row);
    const std::vector<quint64> ids = selectedIds();
    return ids.empty() ? 0 : ids.front();
}

void FixturePanel::selectFixture(quint64 id)
{
    m_updating = true;
    m_table->clearSelection();
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (idAt(r) == id) {
            m_table->selectionModel()->setCurrentIndex(m_table->model()->index(r, ColName),
                                                       QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_table->scrollToItem(m_table->item(r, ColName));
        }
    m_updating = false;
}

void FixturePanel::fillAddMenu()
{
    m_addMenu->clear();
    const FixturePatch p = m_engine->fixturePatch();
    auto add = [this](const FixtureType &t) {
        QAction *a = m_addMenu->addAction(t.pixelCount() > 1 ? QStringLiteral("%1  (%2 px)").arg(t.name).arg(t.pixelCount()) : t.name);
        const QString id = t.id;
        connect(a, &QAction::triggered, this, [this, id] { newFixture(id); });
    };
    for (const FixtureType &t : builtInFixtureTypes()) add(t);
    if (!p.types.empty()) m_addMenu->addSeparator();
    for (const FixtureType &t : p.types) add(t);
    // The templates of this machine: a fixture of one copies its type into the project
    const std::vector<FixtureType> templates = fixtureTemplates();
    if (!templates.empty()) {
        QMenu *list = m_addMenu->addMenu(QStringLiteral("Templates"));
        list->setObjectName(QStringLiteral("fixtureTemplatesMenu"));
        for (const FixtureType &t : templates)
            list->addAction(t.pixelCount() > 1 ? QStringLiteral("%1  (%2 px)").arg(t.name).arg(t.pixelCount()) : t.name, this,
                            [this, t] { newFixtureFrom(t); });
        list->addSeparator();
        QMenu *remove = list->addMenu(QStringLiteral("Remove Template"));
        for (const FixtureType &t : templates)
            remove->addAction(t.name, this, [id = t.id] { removeFixtureTemplate(id); });
    }
    m_addMenu->addSeparator();
    m_addMenu->addAction(QStringLiteral("New Fixture Type…"), this, &FixturePanel::newFixtureType);
    m_addMenu->addAction(QStringLiteral("Import GDTF…"), this, [this] {
        QSettings s;
        const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Import GDTF fixtures"),
                                                                s.value(QStringLiteral("fixtures/gdtfFolder")).toString(),
                                                                QStringLiteral("GDTF fixtures (*.gdtf)"));
        if (paths.isEmpty()) return;
        s.setValue(QStringLiteral("fixtures/gdtfFolder"), QFileInfo(paths.first()).absolutePath());
        QMessageBox::information(this, QStringLiteral("Import GDTF"),
                                 importGdtf(paths, [this](FixtureType &t) { return reviewImported(t); }));
    });
    QAction *clean = m_addMenu->addAction(QStringLiteral("Remove Unused Types"), this, [this] {
        cmd::editFixtures(m_undo, m_engine, QStringLiteral("Remove Unused Fixture Types"), [](FixturePatch &q) {
            q.types.erase(std::remove_if(q.types.begin(), q.types.end(), [&](const FixtureType &t) {
                              return std::none_of(q.fixtures.begin(), q.fixtures.end(), [&](const Fixture &f) { return f.type == t.id; });
                          }),
                          q.types.end());
        });
        emit projectEdited();
    });
    clean->setEnabled(!p.types.empty());
}

void FixturePanel::refresh()
{
    // Not under a rename in progress: tried again a moment later
    if (const QWidget *f = QApplication::focusWidget(); f && m_table->viewport()->isAncestorOf(f) && qobject_cast<const QLineEdit *>(f)) {
        m_refreshTimer->start();
        return;
    }
    const FixturePatch p = m_engine->fixturePatch();
    const quint64 selected = selectedFixture();
    m_updating = true;

    // Address conflicts: two fixtures on one channel of one output's universe
    std::map<std::tuple<quint64, int, int>, int> users;
    std::map<quint64, std::vector<DmxSpan>> footprints;
    for (const Fixture &f : p.fixtures) {
        const std::optional<FixtureType> t = p.resolvedType(f);
        if (!t || !f.enabled) continue;
        footprints[f.id] = fixtureFootprint(*t, f.universe, f.address);
        for (const DmxSpan &s : footprints[f.id])
            for (int a = s.first; a <= s.last; ++a) ++users[{f.output, s.universe, a}];
    }
    const bool anySolo = std::any_of(p.fixtures.begin(), p.fixtures.end(), [](const Fixture &f) { return f.solo && f.enabled; });
    int conflicts = 0, channels = 0;
    std::set<std::pair<quint64, int>> universes;

    // The rows: each group (its header, then its members when it is open), then the fixtures in no group
    struct Entry { bool group; int index; };
    std::vector<Entry> entries;
    for (int gi = 0; gi < int(p.groups.size()); ++gi) {
        entries.push_back({true, gi});
        if (!p.groups[size_t(gi)].open) continue;
        for (int fi = 0; fi < int(p.fixtures.size()); ++fi)
            if (p.fixtures[size_t(fi)].group == p.groups[size_t(gi)].id) entries.push_back({false, fi});
    }
    for (int fi = 0; fi < int(p.fixtures.size()); ++fi)
        if (!p.fixtures[size_t(fi)].group) entries.push_back({false, fi});
    // Rows made again only when their number changes: an intensity being dragged keeps its slider
    if (m_table->rowCount() != int(entries.size())) {
        m_table->clearContents();
        m_table->setRowCount(int(entries.size()));
    }
    static const Fixture kNoFixture; // a group's row has no fixture of its own
    for (int r = 0; r < int(entries.size()); ++r) {
        const Entry entry = entries[size_t(r)];
        const Fixture &f = entry.group ? kNoFixture : p.fixtures[size_t(entry.index)];
        const std::optional<FixtureType> t = p.resolvedType(f);
        auto cell = [&](int col, Qt::ItemFlags flags) {
            QTableWidgetItem *it = m_table->item(r, col);
            if (!it) {
                it = new QTableWidgetItem;
                it->setFlags(flags);
                m_table->setItem(r, col, it);
            }
            return it;
        };
        if (entry.group) { // a group: its own enable, lock, solo and intensity (its fixtures' combine with them)
            const FixtureGroup &g = p.groups[size_t(entry.index)];
            int members = 0;
            for (const Fixture &x : p.fixtures)
                if (x.group == g.id) ++members;
            QTableWidgetItem *on = cell(ColOn, Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            on->setCheckState(g.enabled ? Qt::Checked : Qt::Unchecked);
            on->setToolTip(QStringLiteral("Enable the group: none of its fixtures sends when it is off"));
            QTableWidgetItem *lock = cell(ColLock, Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            lock->setIcon(padlockIcon(g.locked));
            lock->setData(Qt::UserRole, 0);
            lock->setToolTip(g.locked ? QStringLiteral("Group locked — click to unlock it") : QStringLiteral("Click to lock the group"));
            QTableWidgetItem *solo = cell(ColSolo, Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            solo->setText(QStringLiteral("S"));
            solo->setTextAlignment(Qt::AlignCenter);
            QFont sf = solo->font();
            sf.setBold(true);
            solo->setFont(sf);
            solo->setForeground(g.solo ? QColor(255, 214, 90) : QColor(95, 95, 102));
            solo->setToolTip(g.solo ? QStringLiteral("Group soloed — click to show every group again") : QStringLiteral("Solo: only the soloed groups send"));
            QTableWidgetItem *name = cell(ColName, Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            name->setData(Qt::UserRole, quint64(0)); // not a fixture
            name->setData(Qt::UserRole + 1, g.id);    // a group's row
            const QString shown = g.name.isEmpty() ? QStringLiteral("Group") : g.name; // its fold mark drawn before it
            if (name->text() != shown) name->setText(shown);
            name->setData(kGroupOfRole, g.id);
            name->setData(kOpenRole, g.open);
            QFont nf = name->font();
            nf.setBold(true);
            name->setFont(nf);
            name->setForeground(QColor(232, 214, 170));
            name->setToolTip(QStringLiteral("Group of %1 fixture(s) — click to fold or unfold it, double-click to rename").arg(members));
            QWidget *box = m_table->cellWidget(r, ColIntensity);
            SliderField *slider = box ? box->findChild<SliderField *>() : nullptr;
            if (!box) {
                box = makeIntensityBox(&slider);
                m_table->setCellWidget(r, ColIntensity, box);
                connect(slider, &SliderField::valueEdited, this, [this, slider](double value) {
                    if (!m_updating) editIntensity(slider, value);
                });
            }
            attachShowValue(slider, m_engine, m_undo, {QStringLiteral("fixture/g%1/intensity").arg(g.id)});
            const double want = std::round(g.intensity * 100);
            if (!slider->isDragging() && std::abs(slider->value() - want) > 1e-9) slider->setValue(want);
            name->setData(kMetaRole, QStringLiteral("%1 fixture%2").arg(members).arg(members == 1 ? QString() : QStringLiteral("s")));
            cell(ColDmx, Qt::ItemIsEnabled | Qt::ItemIsSelectable)->setText(QString());
            continue;
        }
        QTableWidgetItem *on = cell(ColOn, Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        on->setCheckState(f.enabled ? Qt::Checked : Qt::Unchecked);
        on->setToolTip(QStringLiteral("Enable"));
        QTableWidgetItem *lock = cell(ColLock, Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (lock->data(Qt::UserRole).toInt() != int(f.locked) + 1) {
            lock->setIcon(padlockIcon(f.locked));
            lock->setData(Qt::UserRole, int(f.locked) + 1);
        }
        lock->setToolTip(f.locked ? QStringLiteral("Locked — click to unlock") : QStringLiteral("Click to lock (not moved in the views)"));
        QTableWidgetItem *solo = cell(ColSolo, Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        solo->setText(QStringLiteral("S"));
        solo->setTextAlignment(Qt::AlignCenter);
        QFont sf = solo->font();
        sf.setBold(true);
        solo->setFont(sf);
        solo->setForeground(f.solo ? QColor(255, 214, 90) : QColor(95, 95, 102));
        solo->setToolTip(f.solo ? QStringLiteral("Soloed — click to send every fixture again")
                                : QStringLiteral("Solo: the other fixtures send black"));
        QTableWidgetItem *name = cell(ColName, Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        name->setData(Qt::UserRole, f.id);
        name->setData(Qt::UserRole + 1, quint64(0));
        name->setData(kGroupOfRole, f.group);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        const QString shown = f.name.isEmpty() ? QStringLiteral("Fixture %1").arg(f.id) : f.name;
        if (name->text() != shown) name->setText(shown);
        QFont nf = name->font();
        nf.setBold(true);
        name->setFont(nf);
        name->setForeground(!t                        ? QColor(255, 110, 95)
                            : anySolo && !f.solo     ? QColor(110, 110, 115)
                                                      : QColor(230, 230, 233));
        name->setToolTip(t ? QStringLiteral("%1 — %2 px, %3 channels").arg(t->name).arg(t->pixelCount()).arg(t->channelCount())
                           : QStringLiteral("Missing type: %1").arg(f.type));

        // Intensity: the application's bar, as on the layers
        QWidget *box = m_table->cellWidget(r, ColIntensity);
        SliderField *slider = box ? box->findChild<SliderField *>() : nullptr;
        if (!box) {
            box = makeIntensityBox(&slider);
            m_table->setCellWidget(r, ColIntensity, box);
            connect(slider, &SliderField::valueEdited, this, [this, slider](double value) {
                if (!m_updating) editIntensity(slider, value);
            });
        }
        attachShowValue(slider, m_engine, m_undo, {QStringLiteral("fixture/%1/intensity").arg(f.id)});
        const double pct = std::round(f.intensity * 100);
        if (!slider->isDragging() && std::abs(slider->value() - pct) > 1e-9) slider->setValue(pct);

        name->setData(kMetaRole, t ? QStringLiteral("%1 · %2 px · %3 ch").arg(t->name).arg(t->pixelCount()).arg(t->channelCount())
                                   : QStringLiteral("Missing type"));
        QTableWidgetItem *dmx = cell(ColDmx, Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        dmx->setText(fixtureview::address(p, f));
        bool clash = false;
        for (const DmxSpan &s : footprints[f.id]) {
            for (int a = s.first; a <= s.last; ++a) clash |= users[{f.output, s.universe, a}] > 1;
            channels += s.last - s.first + 1;
            universes.insert({f.output, s.universe});
        }
        const DmxOutput *out = p.findOutput(f.output);
        dmx->setForeground(clash ? QColor(255, 110, 95) : out ? QColor(200, 200, 205) : QColor(120, 120, 125));
        dmx->setToolTip(clash ? QStringLiteral("Its channels overlap another fixture's on this output")
                        : out ? out->name
                              : QStringLiteral("No output: nothing sent"));
        conflicts += clash;
    }
    m_summary->setText(QStringLiteral("%1 fixtures, %2 channels in %3 universes%4")
                           .arg(p.fixtures.size()).arg(channels).arg(universes.size())
                           .arg(conflicts ? QStringLiteral(" — <span style='color:#e66'>%1 in conflict</span>").arg(conflicts) : QString()));
    m_updating = false;
    selectFixture(selected);
}

quint64 FixturePanel::addFixture(const QString &typeId)
{
    const QSize comp = m_engine->compositionSize();
    quint64 made = 0;
    const quint64 into = selectedGroup(); // a fixture made while a group is selected joins it, and obeys its values
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("New Fixture"), [&](FixturePatch &p) {
        const FixtureType *t = p.findType(typeId);
        if (!t) return;
        if (p.outputs.empty()) { // a first fixture: an output to send it
            DmxOutput o;
            o.name = QStringLiteral("Art-Net 1");
            p.outputs.push_back(o);
            p.normalize();
        }
        Fixture f;
        f.type = typeId;
        f.output = p.outputs.front().id;
        std::tie(f.universe, f.address) = nextFree(p, f.output, t->channelCount());
        f.name = QStringLiteral("%1 %2").arg(t->name).arg(p.fixtures.size() + 1);
        // Square cells, a third of the composition wide at most
        const double cw = std::max(1, comp.width()), ch = std::max(1, comp.height());
        const double cell = std::min(cw / 3 / t->cols, ch / 3 / t->rows);
        f.size = QSizeF(cell * t->cols / cw, cell * t->rows / ch);
        for (const Fixture &x : p.fixtures) made = std::max(made, x.id);
        f.id = ++made;
        f.group = p.findGroup(into) ? into : 0;
        p.fixtures.push_back(f);
    });
    if (made) {
        refresh();
        selectFixture(made);
        emit fixtureSelected(made);
        emit projectEdited();
    }
    return made;
}

int FixturePanel::addLineFixtures(quint64 layer, const QString &typeId, int pixels)
{
    std::vector<std::vector<QPointF>> paths; // where each line is on the composition now (the engine keeps it so)
    std::vector<bool> closed;
    QString layerName;
    {
        Engine::Lock lk(&m_engine->mutex());
        const Layer *l = m_engine->layer(m_engine->indexOfId(layer));
        if (!l) return 0;
        layerName = l->name;
        for (const LinePath &line : l->lines) {
            std::vector<QPointF> pts;
            for (const QPointF &q : line.points) pts.push_back(l->mapping.map(q.x(), q.y()));
            paths.push_back(pts);
            closed.push_back(line.closed);
        }
    }
    if (paths.empty()) return 0;
    int count = 0;
    quint64 last = 0;
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Fixtures on Lines"), [&](FixturePatch &p) {
        const FixtureType *t = p.findType(typeId);
        if (!t || !takesFixtureGrid(*t)) return;
        if (p.outputs.empty()) {
            DmxOutput o;
            o.name = QStringLiteral("Art-Net 1");
            p.outputs.push_back(o);
            p.normalize();
        }
        quint64 next = 0;
        for (const Fixture &x : p.fixtures) next = std::max(next, x.id);
        for (int i = 0; i < int(paths.size()); ++i) {
            Fixture f;
            f.type = typeId;
            f.output = p.outputs.front().id;
            f.cols = std::clamp(pixels, 1, 4096);
            f.rows = 1;
            f.path = paths[size_t(i)];
            f.pathClosed = closed[size_t(i)];
            f.lineLayer = layer;
            f.lineIndex = i;
            const std::optional<FixtureType> r = p.resolvedType(f);
            std::tie(f.universe, f.address) = nextFree(p, f.output, r ? r->channelCount() : t->channelCount());
            f.name = QStringLiteral("%1 · line %2").arg(layerName).arg(i + 1);
            f.id = last = ++next;
            p.fixtures.push_back(f);
            ++count;
        }
    });
    if (count) {
        refresh();
        selectFixture(last);
        emit fixtureSelected(last);
        emit projectEdited();
    }
    return count;
}

bool FixturePanel::hasFocusInside() const
{
    return m_table->hasFocus() || m_table->isAncestorOf(QApplication::focusWidget());
}

void FixturePanel::duplicateFixtures()
{
    const std::vector<quint64> chosen = selectedIds();
    const std::set<quint64> ids(chosen.begin(), chosen.end());
    if (ids.empty()) return;
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Duplicate Fixtures"), [&](FixturePatch &p) {
        quint64 next = 0;
        for (const Fixture &x : p.fixtures) next = std::max(next, x.id);
        const std::vector<Fixture> source = p.fixtures;
        for (const Fixture &f : source) {
            if (!ids.count(f.id)) continue;
            Fixture copy = f;
            copy.id = ++next;
            QStringList taken;
            for (const Fixture &other : p.fixtures) taken << other.name;
            copy.name = numberedName(copy.name.isEmpty() ? QStringLiteral("Fixture") : copy.name, taken);
            copy.solo = false;
            copy.center += QPointF(0, f.size.height() * 1.5); // just under it
            if (const std::optional<FixtureType> t = p.resolvedType(f))
                std::tie(copy.universe, copy.address) = nextFree(p, f.output, t->channelCount());
            p.fixtures.push_back(copy);
        }
    });
    emit projectEdited();
}

void FixturePanel::removeFixtures()
{
    const std::vector<quint64> chosen = selectedIds();
    const std::set<quint64> ids(chosen.begin(), chosen.end());
    if (ids.empty()) return;
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Delete Fixtures"), [&](FixturePatch &p) {
        p.fixtures.erase(std::remove_if(p.fixtures.begin(), p.fixtures.end(), [&](const Fixture &f) { return ids.count(f.id) > 0; }),
                         p.fixtures.end());
    });
    emit fixtureSelected(0);
    emit projectEdited();
}

QString FixturePanel::importGdtf(const QStringList &paths, const std::function<bool(FixtureType &)> &review)
{
    std::vector<FixtureType> types;
    QStringList report;
    for (const QString &path : paths) {
        const gdtf::Import im = gdtf::readFile(path);
        if (!im.error.isEmpty()) report << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), im.error);
        for (FixtureType t : im.types) {
            if (review && !review(t)) {
                report << QStringLiteral("Left out %1").arg(t.name);
                continue;
            }
            report << QStringLiteral("Imported %1 (%2 px, %3 channels)").arg(t.name).arg(t.pixelCount()).arg(t.channelCount());
            types.push_back(t);
        }
        report += im.warnings;
    }
    if (!types.empty()) {
        cmd::editFixtures(m_undo, m_engine, QStringLiteral("Import GDTF"), [&](FixturePatch &p) {
            for (const FixtureType &t : types) {
                const auto same = std::find_if(p.types.begin(), p.types.end(), [&](const FixtureType &x) { return x.id == t.id; });
                if (same != p.types.end()) *same = t;
                else p.types.push_back(t);
            }
        });
        emit projectEdited();
    }
    return report.join(QLatin1Char('\n'));
}

namespace {
// The channel editor in a dialog: what each channel follows (pixel mapping or a fixed value), channels added
bool chooseChannels(QWidget *parent, const QString &title, const QString &accept, const QString &reject,
                    std::vector<ChannelEditor::Row> &rows)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("fixtureChannelsDialog"));
    dialog.setWindowTitle(title);
    auto *v = new QVBoxLayout(&dialog);
    auto *say = new QLabel(QStringLiteral("Each channel follows the pixel mapping (a colour, a dimmer) or sends a fixed value, "
                                          "as it is: a mode, a shutter open, a macro at 0. + Channel adds one after the last."));
    say->setWordWrap(true);
    v->addWidget(say);
    auto *editor = new ChannelEditor;
    editor->setRows(rows);
    v->addWidget(editor, 1);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(accept, QDialogButtonBox::AcceptRole)->setDefault(true);
    buttons->addButton(reject, QDialogButtonBox::RejectRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    v->addWidget(buttons);
    dialog.resize(440, 480);
    if (dialog.exec() != QDialog::Accepted) return false;
    rows = editor->rows();
    return true;
}
} // namespace

quint64 FixturePanel::newFixture(const QString &typeId)
{
    const FixturePatch p = m_engine->fixturePatch();
    const FixtureType *type = p.findType(typeId);
    return type ? newFixtureFrom(*type) : 0;
}

quint64 FixturePanel::newFixtureFrom(const FixtureType &type)
{
    std::vector<ChannelEditor::Row> rows = ChannelEditor::rowsOf(type);
    if (!chooseChannels(this, QStringLiteral("New Fixture — %1").arg(type.name), QStringLiteral("Create"), QStringLiteral("Cancel"), rows))
        return 0;
    return addFixtureOfType(type, ChannelEditor::ownChannels(rows));
}

quint64 FixturePanel::addFixtureOfType(const FixtureType &type, const std::map<int, FixtureChannel> &own)
{
    m_undo->beginMacro(QStringLiteral("New Fixture"));
    const FixturePatch p = m_engine->fixturePatch();
    if (!type.builtIn && !p.findType(type.id)) // a template, or a type made here: into the project
        cmd::editFixtures(m_undo, m_engine, QStringLiteral("Fixture Type"), [&](FixturePatch &q) { q.types.push_back(type); });
    const quint64 id = addFixture(type.id);
    if (id && !own.empty())
        cmd::editFixtures(m_undo, m_engine, QStringLiteral("Fixture Channels"), [&](FixturePatch &q) {
            if (Fixture *f = q.findFixture(id)) f->channels = own;
        });
    m_undo->endMacro();
    return id;
}

void FixturePanel::newFixtureType()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("newFixtureTypeDialog"));
    dialog.setWindowTitle(QStringLiteral("New Fixture Type"));
    auto *v = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(QStringLiteral("My Fixture"));
    auto *maker = new QLineEdit;
    maker->setPlaceholderText(QStringLiteral("optional"));
    auto *cols = new QSpinBox;
    auto *rows = new QSpinBox;
    for (QSpinBox *b : {cols, rows}) b->setRange(1, 1024);
    auto *grid = new QHBoxLayout;
    grid->addWidget(cols);
    grid->addWidget(new QLabel(QStringLiteral("×")));
    grid->addWidget(rows);
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Manufacturer"), maker);
    form->addRow(QStringLiteral("Pixels"), grid);
    v->addLayout(form);
    // Its channels: those of the whole fixture first, then those of each pixel, repeated pixel after pixel
    auto *wholeLabel = new QLabel(QStringLiteral("<b>Channels of the whole fixture</b> — before its pixels (a dimmer, a mode, a shutter)"));
    wholeLabel->setWordWrap(true);
    v->addWidget(wholeLabel);
    auto *whole = new ChannelEditor;
    v->addWidget(whole, 1);
    auto *pixelLabel = new QLabel(QStringLiteral("<b>Channels of each pixel</b> — repeated for every pixel, in DMX order"));
    pixelLabel->setWordWrap(true);
    v->addWidget(pixelLabel);
    auto *pixel = new ChannelEditor;
    pixel->setRows({{1, -1, FixtureChannel{ChannelRole::Red, false, 0}, std::nullopt},
                    {2, -1, FixtureChannel{ChannelRole::Green, false, 0}, std::nullopt},
                    {3, -1, FixtureChannel{ChannelRole::Blue, false, 0}, std::nullopt}});
    v->addWidget(pixel, 1);
    auto *keep = new QCheckBox(QStringLiteral("Save as template (offered in every project)"));
    keep->setChecked(true);
    v->addWidget(keep);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(QStringLiteral("Create"), QDialogButtonBox::AcceptRole)->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    v->addWidget(buttons);
    dialog.resize(480, 640);
    while (dialog.exec() == QDialog::Accepted) {
        FixtureType t;
        t.id = QStringLiteral("custom-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(10));
        t.name = name->text().trimmed().isEmpty() ? QStringLiteral("My Fixture") : name->text().trimmed();
        t.manufacturer = maker->text().trimmed();
        t.cols = cols->value();
        t.rows = rows->value();
        for (const ChannelEditor::Row &r : whole->rows()) t.header.push_back({r.now.role, false, r.now.value});
        for (const ChannelEditor::Row &r : pixel->rows()) t.perPixel.push_back({r.now.role, false, r.now.value});
        QString why;
        if (!t.valid(&why)) {
            QMessageBox::warning(&dialog, QStringLiteral("New Fixture Type"), QStringLiteral("This type cannot be made: %1.").arg(why));
            continue;
        }
        if (keep->isChecked() && !saveFixtureTemplate(t, &why))
            QMessageBox::warning(this, QStringLiteral("New Fixture Type"), QStringLiteral("Not saved as a template: %1").arg(why));
        addFixtureOfType(t);
        return;
    }
}

bool FixturePanel::reviewImported(FixtureType &type)
{
    std::vector<ChannelEditor::Row> rows = ChannelEditor::rowsOf(type);
    if (!chooseChannels(this, QStringLiteral("Import — %1").arg(type.name), QStringLiteral("Import"), QStringLiteral("Leave Out"), rows))
        return false;
    type = ChannelEditor::withRows(type, rows);
    return true;
}
