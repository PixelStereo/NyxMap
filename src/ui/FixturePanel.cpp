#include "FixturePanel.h"
#include "Commands.h"
#include "Engine.h"
#include "FixtureOverlay.h"
#include "Gdtf.h"
#include "Widgets.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace {
enum Column { ColOn, ColLock, ColSolo, ColName, ColIntensity, ColDmx, ColCount };

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

    m_table = new QTableWidget(0, ColCount);
    m_table->setObjectName(QStringLiteral("fixturesTable"));
    m_table->setHorizontalHeaderLabels({QString(), QString(), QString(), QStringLiteral("Fixture"),
                                        QStringLiteral("Intensity"), QStringLiteral("DMX")});
    m_table->horizontalHeaderItem(ColOn)->setToolTip(QStringLiteral("Enable"));
    m_table->horizontalHeaderItem(ColLock)->setToolTip(QStringLiteral("Locked: not moved in the views"));
    m_table->horizontalHeaderItem(ColSolo)->setToolTip(QStringLiteral("Solo: the other fixtures send black"));
    m_table->horizontalHeaderItem(ColIntensity)->setToolTip(QStringLiteral("A factor on everything the fixture sends"));
    m_table->horizontalHeaderItem(ColDmx)->setToolTip(QStringLiteral("Universe · channels"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(26);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setIconSize(QSize(16, 16));
    auto *h = m_table->horizontalHeader();
    h->setHighlightSections(false);
    for (auto [col, width] : {std::pair{ColOn, 30}, {ColLock, 28}, {ColSolo, 26}, {ColIntensity, 150}}) {
        h->setSectionResizeMode(col, QHeaderView::Fixed);
        m_table->setColumnWidth(col, width);
    }
    h->setSectionResizeMode(ColName, QHeaderView::Interactive);
    m_table->setColumnWidth(ColName, 170);
    h->setStretchLastSection(true);
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
    for (const QModelIndex &i : m_table->selectionModel()->selectedRows()) ids.push_back(idAt(i.row()));
    return ids;
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
        connect(a, &QAction::triggered, this, [this, id] { addFixture(id); });
    };
    for (const FixtureType &t : builtInFixtureTypes()) add(t);
    if (!p.types.empty()) m_addMenu->addSeparator();
    for (const FixtureType &t : p.types) add(t);
    m_addMenu->addSeparator();
    m_addMenu->addAction(QStringLiteral("Import GDTF…"), this, [this] {
        QSettings s;
        const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Import GDTF fixtures"),
                                                                s.value(QStringLiteral("fixtures/gdtfFolder")).toString(),
                                                                QStringLiteral("GDTF fixtures (*.gdtf)"));
        if (paths.isEmpty()) return;
        s.setValue(QStringLiteral("fixtures/gdtfFolder"), QFileInfo(paths.first()).absolutePath());
        QMessageBox::information(this, QStringLiteral("Import GDTF"), importGdtf(paths));
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

    // Rows made again only when their number changes: an intensity being dragged keeps its slider
    if (m_table->rowCount() != int(p.fixtures.size())) {
        m_table->clearContents();
        m_table->setRowCount(int(p.fixtures.size()));
    }
    for (int r = 0; r < int(p.fixtures.size()); ++r) {
        const Fixture &f = p.fixtures[size_t(r)];
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
        QTableWidgetItem *on = cell(ColOn, Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
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
            box = new QWidget;
            auto *hl = new QHBoxLayout(box);
            hl->setContentsMargins(4, 0, 4, 0);
            slider = new SliderField;
            slider->setObjectName(QStringLiteral("fixtureIntensity"));
            slider->setRange(0, 100);
            slider->setDecimals(0);
            slider->setSuffix(QStringLiteral(" %"));
            slider->setSingleStep(1);
            slider->setSnaps({100});
            slider->setCompact(true);
            slider->setFocusPolicy(Qt::NoFocus);
            hl->addWidget(slider, 1);
            m_table->setCellWidget(r, ColIntensity, box);
            connect(slider, &SliderField::valueEdited, this, [this, slider](double value) {
                if (m_updating) return;
                for (int k = 0; k < m_table->rowCount(); ++k)
                    if (QWidget *c = m_table->cellWidget(k, ColIntensity); c && c->isAncestorOf(slider)) {
                        const quint64 id = idAt(k);
                        cmd::editFixtures(m_undo, m_engine, QStringLiteral("Fixture Intensity"), [&](FixturePatch &q) {
                            for (Fixture &x : q.fixtures)
                                if (x.id == id) x.intensity = value / 100.0;
                        }, QStringLiteral("fixture-intensity-%1").arg(id));
                        emit projectEdited();
                        break;
                    }
            });
        }
        const double pct = std::round(f.intensity * 100);
        if (!slider->isDragging() && std::abs(slider->value() - pct) > 1e-9) slider->setValue(pct);

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

QString FixturePanel::importGdtf(const QStringList &paths)
{
    std::vector<FixtureType> types;
    QStringList report;
    for (const QString &path : paths) {
        const gdtf::Import im = gdtf::readFile(path);
        if (!im.error.isEmpty()) report << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), im.error);
        for (const FixtureType &t : im.types)
            report << QStringLiteral("Imported %1 (%2 px, %3 channels)").arg(t.name).arg(t.pixelCount()).arg(t.channelCount());
        report += im.warnings;
        types.insert(types.end(), im.types.begin(), im.types.end());
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
