#include "FixturePanel.h"
#include "Commands.h"
#include "Engine.h"
#include "Gdtf.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>
#include <map>
#include <set>

namespace {
enum FixtureColumn { FOn, FName, FType, FOutput, FUniverse, FAddress, FChannels, FX, FY, FW, FH, FRot, FIntensity, FCount };
enum OutputColumn { OOn, OName, OProtocol, ODestination, OInterface, ORate, OState, OCount };

QTableWidgetItem *item(const QString &text, bool editable = true)
{
    auto *i = new QTableWidgetItem(text);
    if (!editable) i->setFlags(i->flags() & ~Qt::ItemIsEditable);
    return i;
}

QTableWidgetItem *check(bool on)
{
    auto *i = new QTableWidgetItem;
    i->setFlags((i->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
    i->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    return i;
}

QPushButton *smallButton(const QString &text, const QString &tip)
{
    auto *b = new QPushButton(text);
    b->setToolTip(tip);
    if (text.size() <= 1) b->setFixedWidth(30);
    return b;
}

QString number(double v, int decimals = 1) { return QString::number(v, 'f', decimals); }

// The next address free on an output, after every fixture already patched to it
std::pair<int, int> nextFree(const FixturePatch &p, quint64 output, int channels)
{
    int universe = 1, address = 1;
    bool any = false;
    for (const Fixture &f : p.fixtures) {
        const FixtureType *t = p.findType(f.type);
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
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    // --- Outputs
    auto *outHeader = new QHBoxLayout;
    outHeader->addWidget(new QLabel(QStringLiteral("<b>DMX outputs</b>")));
    auto *addOut = smallButton(QStringLiteral("+"), QStringLiteral("New DMX output (Art-Net or sACN)"));
    auto *removeOut = smallButton(QStringLiteral("−"), QStringLiteral("Delete the selected output"));
    addOut->setObjectName(QStringLiteral("fixturesAddOutput"));
    outHeader->addStretch(1);
    outHeader->addWidget(addOut);
    outHeader->addWidget(removeOut);
    layout->addLayout(outHeader);
    m_outputs = new QTableWidget(0, OCount);
    m_outputs->setObjectName(QStringLiteral("fixturesOutputs"));
    m_outputs->setHorizontalHeaderLabels({QStringLiteral("On"), QStringLiteral("Name"), QStringLiteral("Protocol"),
                                          QStringLiteral("Destination"), QStringLiteral("Interface"), QStringLiteral("Rate"),
                                          QStringLiteral("State")});
    m_outputs->horizontalHeaderItem(ODestination)->setToolTip(QStringLiteral("An IP address; empty: broadcast (Art-Net) or multicast (sACN)"));
    QStringList locals;
    for (const QHostAddress &a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol) locals << a.toString();
    m_outputs->horizontalHeaderItem(OInterface)->setToolTip(
        QStringLiteral("The local address sent from (its network card); empty: any.\nThis machine: %1").arg(locals.join(QStringLiteral(", "))));
    m_outputs->horizontalHeaderItem(ORate)->setToolTip(QStringLiteral("Packets per second and universe"));
    m_outputs->verticalHeader()->hide();
    m_outputs->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_outputs->setSelectionMode(QAbstractItemView::SingleSelection);
    m_outputs->horizontalHeader()->setStretchLastSection(true);
    m_outputs->setMaximumHeight(130);
    layout->addWidget(m_outputs);

    // --- Fixtures
    auto *fxHeader = new QHBoxLayout;
    fxHeader->addWidget(new QLabel(QStringLiteral("<b>Fixtures</b>")));
    fxHeader->addStretch(1);
    m_newType = new QComboBox;
    m_newType->setObjectName(QStringLiteral("fixturesNewType"));
    m_newType->setToolTip(QStringLiteral("The type of the next fixture added"));
    auto *add = smallButton(QStringLiteral("+"), QStringLiteral("New fixture of this type, at the next free address"));
    add->setObjectName(QStringLiteral("fixturesAdd"));
    auto *dup = smallButton(QStringLiteral("Duplicate"), QStringLiteral("Copies of the selected fixtures, at the next free addresses"));
    auto *remove = smallButton(QStringLiteral("−"), QStringLiteral("Delete the selected fixtures"));
    fxHeader->addWidget(m_newType);
    fxHeader->addWidget(add);
    fxHeader->addWidget(dup);
    fxHeader->addWidget(remove);
    layout->addLayout(fxHeader);
    m_fixtures = new QTableWidget(0, FCount);
    m_fixtures->setObjectName(QStringLiteral("fixturesTable"));
    m_fixtures->setHorizontalHeaderLabels({QStringLiteral("On"), QStringLiteral("Name"), QStringLiteral("Type"),
                                           QStringLiteral("Output"), QStringLiteral("Univ."), QStringLiteral("Addr."),
                                           QStringLiteral("Channels"), QStringLiteral("X"), QStringLiteral("Y"),
                                           QStringLiteral("W"), QStringLiteral("H"), QStringLiteral("Rot."), QStringLiteral("Int.")});
    for (int c : {FX, FY, FW, FH})
        m_fixtures->horizontalHeaderItem(c)->setToolTip(QStringLiteral("Center and size, in composition pixels"));
    m_fixtures->horizontalHeaderItem(FRot)->setToolTip(QStringLiteral("Rotation, degrees clockwise"));
    m_fixtures->horizontalHeaderItem(FIntensity)->setToolTip(QStringLiteral("Intensity, 0..100 %"));
    m_fixtures->horizontalHeaderItem(FUniverse)->setToolTip(QStringLiteral("Art-Net: port address from 0; sACN: 1..63999"));
    m_fixtures->verticalHeader()->hide();
    m_fixtures->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fixtures->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_fixtures, 1);
    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    // --- Types
    auto *typeHeader = new QHBoxLayout;
    typeHeader->addWidget(new QLabel(QStringLiteral("<b>Fixture types of the project</b>")));
    typeHeader->addStretch(1);
    auto *import = smallButton(QStringLiteral("Import GDTF…"), QStringLiteral("Each DMX mode of a .gdtf file becomes a fixture type"));
    import->setObjectName(QStringLiteral("fixturesImportGdtf"));
    auto *removeType = smallButton(QStringLiteral("−"), QStringLiteral("Delete the selected type (not while a fixture uses it)"));
    typeHeader->addWidget(import);
    typeHeader->addWidget(removeType);
    layout->addLayout(typeHeader);
    m_types = new QListWidget;
    m_types->setObjectName(QStringLiteral("fixturesTypes"));
    m_types->setMaximumHeight(100);
    layout->addWidget(m_types);

    connect(addOut, &QPushButton::clicked, this, &FixturePanel::addOutput);
    connect(removeOut, &QPushButton::clicked, this, &FixturePanel::removeOutput);
    connect(add, &QPushButton::clicked, this, &FixturePanel::addFixture);
    connect(dup, &QPushButton::clicked, this, &FixturePanel::duplicateFixture);
    connect(remove, &QPushButton::clicked, this, &FixturePanel::removeFixtures);
    connect(removeType, &QPushButton::clicked, this, &FixturePanel::removeType);
    connect(import, &QPushButton::clicked, this, [this] {
        QSettings s;
        const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Import GDTF fixtures"),
                                                                s.value(QStringLiteral("fixtures/gdtfFolder")).toString(),
                                                                QStringLiteral("GDTF fixtures (*.gdtf)"));
        if (paths.isEmpty()) return;
        s.setValue(QStringLiteral("fixtures/gdtfFolder"), QFileInfo(paths.first()).absolutePath());
        QMessageBox::information(this, QStringLiteral("Import GDTF"), importGdtf(paths));
    });
    connect(m_fixtures, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *i) {
        if (!m_updating) editFixtureCell(i->row(), i->column());
    });
    connect(m_outputs, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *i) {
        if (!m_updating) editOutputCell(i->row(), i->column());
    });
    connect(m_fixtures, &QTableWidget::itemSelectionChanged, this, [this] {
        if (!m_updating) emit fixtureSelected(selectedFixture());
    });

    // The tables follow the engine; a burst of changes (a fixture dragged in the view) is one refresh
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(60);
    connect(m_refreshTimer, &QTimer::timeout, this, &FixturePanel::refresh);
    connect(m_engine, &Engine::fixturesChanged, m_refreshTimer, qOverload<>(&QTimer::start));
    connect(m_engine, &Engine::compositionSizeChanged, m_refreshTimer, qOverload<>(&QTimer::start));
    m_stateTimer = new QTimer(this);
    m_stateTimer->setInterval(1000);
    connect(m_stateTimer, &QTimer::timeout, this, &FixturePanel::refreshStates);
    m_stateTimer->start();
    refresh();
}

quint64 FixturePanel::selectedFixture() const
{
    const auto rows = m_fixtures->selectionModel()->selectedRows();
    if (rows.isEmpty()) return 0;
    const QTableWidgetItem *i = m_fixtures->item(rows.first().row(), FName);
    return i ? i->data(Qt::UserRole).toULongLong() : 0;
}

void FixturePanel::selectFixture(quint64 id)
{
    QSignalBlocker block(m_fixtures);
    m_updating = true;
    m_fixtures->clearSelection();
    for (int r = 0; r < m_fixtures->rowCount(); ++r)
        if (m_fixtures->item(r, FName) && m_fixtures->item(r, FName)->data(Qt::UserRole).toULongLong() == id) {
            m_fixtures->selectRow(r);
            m_fixtures->scrollToItem(m_fixtures->item(r, FName));
        }
    m_updating = false;
}

void FixturePanel::refresh()
{
    // Not under an edit in progress: tried again a moment later
    // (an editor open in a cell: a line edit inside the table has the focus)
    auto editing = [](const QTableWidget *t) {
        const QWidget *f = QApplication::focusWidget();
        return f && t->viewport()->isAncestorOf(f) && qobject_cast<const QLineEdit *>(f);
    };
    if (editing(m_fixtures) || editing(m_outputs)) {
        m_refreshTimer->start();
        return;
    }
    const FixturePatch p = m_engine->fixturePatch();
    const QSize comp = m_engine->compositionSize();
    const quint64 selected = selectedFixture();
    const int outRow = m_outputs->currentRow();
    m_updating = true;

    // Types offered: the built-in ones, then the project's
    {
        const QString current = m_newType->currentData().toString();
        QSignalBlocker block(m_newType);
        m_newType->clear();
        for (const FixtureType &t : builtInFixtureTypes()) m_newType->addItem(t.name, t.id);
        for (const FixtureType &t : p.types) m_newType->addItem(t.name, t.id);
        const int at = m_newType->findData(current);
        m_newType->setCurrentIndex(at >= 0 ? at : 0);
    }
    m_types->clear();
    for (const FixtureType &t : p.types) {
        auto *i = new QListWidgetItem(QStringLiteral("%1 — %2 px, %3 ch").arg(t.name).arg(t.pixelCount()).arg(t.channelCount()));
        i->setData(Qt::UserRole, t.id);
        i->setToolTip(t.manufacturer + (t.origin.isEmpty() ? QString() : QStringLiteral("\n") + t.origin.value("file").toString()));
        m_types->addItem(i);
    }

    // Outputs (rows made again: their combo boxes with them)
    m_outputs->setRowCount(0);
    m_outputs->setRowCount(int(p.outputs.size()));
    for (int r = 0; r < int(p.outputs.size()); ++r) {
        const DmxOutput &o = p.outputs[size_t(r)];
        m_outputs->setItem(r, OOn, check(o.enabled));
        auto *name = item(o.name.isEmpty() ? QStringLiteral("Output %1").arg(o.id) : o.name);
        name->setData(Qt::UserRole, o.id);
        m_outputs->setItem(r, OName, name);
        auto *proto = new QComboBox;
        proto->addItems({QStringLiteral("Art-Net"), QStringLiteral("sACN")});
        proto->setCurrentIndex(o.protocol == DmxOutput::Protocol::Sacn ? 1 : 0);
        const quint64 id = o.id;
        connect(proto, &QComboBox::currentIndexChanged, this, [this, id](int index) {
            cmd::editFixtures(m_undo, m_engine, QStringLiteral("Output Protocol"), [&](FixturePatch &q) {
                for (DmxOutput &x : q.outputs)
                    if (x.id == id) x.protocol = index == 1 ? DmxOutput::Protocol::Sacn : DmxOutput::Protocol::ArtNet;
            });
            emit projectEdited();
        });
        m_outputs->setCellWidget(r, OProtocol, proto);
        m_outputs->setItem(r, ODestination, item(o.destination));
        m_outputs->setItem(r, OInterface, item(o.interfaceAddress));
        m_outputs->setItem(r, ORate, item(number(o.rate, 0)));
        m_outputs->setItem(r, OState, item(QString(), false));
    }
    if (outRow >= 0 && outRow < m_outputs->rowCount()) m_outputs->selectRow(outRow);

    // Address conflicts: two fixtures on one channel of one output's universe
    std::map<std::tuple<quint64, int, int>, int> users;
    std::map<quint64, std::vector<DmxSpan>> footprints;
    for (const Fixture &f : p.fixtures) {
        const FixtureType *t = p.findType(f.type);
        if (!t || !f.enabled) continue;
        footprints[f.id] = fixtureFootprint(*t, f.universe, f.address);
        for (const DmxSpan &s : footprints[f.id])
            for (int a = s.first; a <= s.last; ++a) ++users[{f.output, s.universe, a}];
    }
    int conflicts = 0, channels = 0;
    std::set<std::pair<quint64, int>> universes;

    m_fixtures->setRowCount(0);
    m_fixtures->setRowCount(int(p.fixtures.size()));
    for (int r = 0; r < int(p.fixtures.size()); ++r) {
        const Fixture &f = p.fixtures[size_t(r)];
        const FixtureType *t = p.findType(f.type);
        const quint64 id = f.id;
        m_fixtures->setItem(r, FOn, check(f.enabled));
        auto *name = item(f.name);
        name->setData(Qt::UserRole, f.id);
        m_fixtures->setItem(r, FName, name);
        auto *type = new QComboBox;
        for (int k = 0; k < m_newType->count(); ++k) type->addItem(m_newType->itemText(k), m_newType->itemData(k));
        if (!t) type->addItem(QStringLiteral("(missing: %1)").arg(f.type), f.type);
        type->setCurrentIndex(type->findData(f.type));
        connect(type, &QComboBox::currentIndexChanged, this, [this, id, type] {
            const QString typeId = type->currentData().toString();
            cmd::editFixtures(m_undo, m_engine, QStringLiteral("Fixture Type"), [&](FixturePatch &q) {
                for (Fixture &x : q.fixtures)
                    if (x.id == id) x.type = typeId;
            });
            emit projectEdited();
        });
        m_fixtures->setCellWidget(r, FType, type);
        auto *output = new QComboBox;
        output->addItem(QStringLiteral("—"), QVariant::fromValue<quint64>(0));
        for (const DmxOutput &o : p.outputs)
            output->addItem(o.name.isEmpty() ? QStringLiteral("Output %1").arg(o.id) : o.name, QVariant::fromValue(o.id));
        output->setCurrentIndex(std::max(0, output->findData(QVariant::fromValue(f.output))));
        connect(output, &QComboBox::currentIndexChanged, this, [this, id, output] {
            const quint64 out = output->currentData().toULongLong();
            cmd::editFixtures(m_undo, m_engine, QStringLiteral("Fixture Output"), [&](FixturePatch &q) {
                for (Fixture &x : q.fixtures)
                    if (x.id == id) x.output = out;
            });
            emit projectEdited();
        });
        m_fixtures->setCellWidget(r, FOutput, output);
        m_fixtures->setItem(r, FUniverse, item(QString::number(f.universe)));
        auto *address = item(QString::number(f.address));
        m_fixtures->setItem(r, FAddress, address);
        QStringList spans;
        bool clash = false;
        for (const DmxSpan &s : footprints[f.id]) {
            spans << (s.first == s.last ? QStringLiteral("%1.%2").arg(s.universe).arg(s.first)
                                        : QStringLiteral("%1.%2-%3").arg(s.universe).arg(s.first).arg(s.last));
            for (int a = s.first; a <= s.last; ++a) clash |= users[{f.output, s.universe, a}] > 1;
            channels += s.last - s.first + 1;
            universes.insert({f.output, s.universe});
        }
        auto *chs = item(t ? QStringLiteral("%1 (%2)").arg(t->channelCount()).arg(spans.join(QStringLiteral(", "))) : QString(), false);
        m_fixtures->setItem(r, FChannels, chs);
        if (clash) {
            ++conflicts;
            for (QTableWidgetItem *i : {address, chs}) {
                i->setBackground(QColor(160, 40, 40));
                i->setToolTip(QStringLiteral("Its channels overlap another fixture's on this output"));
            }
        }
        m_fixtures->setItem(r, FX, item(number(f.center.x() * comp.width())));
        m_fixtures->setItem(r, FY, item(number(f.center.y() * comp.height())));
        m_fixtures->setItem(r, FW, item(number(f.size.width() * comp.width())));
        m_fixtures->setItem(r, FH, item(number(f.size.height() * comp.height())));
        m_fixtures->setItem(r, FRot, item(number(f.rotation)));
        m_fixtures->setItem(r, FIntensity, item(number(f.intensity * 100, 0)));
    }
    m_fixtures->resizeColumnsToContents();
    m_summary->setText(QStringLiteral("%1 fixtures, %2 channels in %3 universes%4")
                           .arg(p.fixtures.size()).arg(channels).arg(universes.size())
                           .arg(conflicts ? QStringLiteral(" — <span style='color:#e66'>%1 in conflict</span>").arg(conflicts) : QString()));
    m_updating = false;
    selectFixture(selected);
    refreshStates();
}

void FixturePanel::refreshStates()
{
    if (!isVisible()) return;
    m_updating = true;
    for (int r = 0; r < m_outputs->rowCount(); ++r) {
        QTableWidgetItem *name = m_outputs->item(r, OName), *state = m_outputs->item(r, OState);
        if (!name || !state) continue;
        const DmxOutputState s = m_engine->dmxOutputState(name->data(Qt::UserRole).toULongLong());
        state->setText(s.level == DmxOutputState::Off     ? QStringLiteral("Off")
                       : s.level == DmxOutputState::Error ? QStringLiteral("Error")
                                                          : QStringLiteral("Sending · %1 packets").arg(s.packets));
        state->setToolTip(s.text);
        state->setForeground(s.level == DmxOutputState::Error ? QColor(230, 90, 90) : palette().color(QPalette::Text));
    }
    m_updating = false;
}

void FixturePanel::editFixtureCell(int row, int column)
{
    const QTableWidgetItem *name = m_fixtures->item(row, FName), *cell = m_fixtures->item(row, column);
    if (!name || !cell) return;
    const quint64 id = name->data(Qt::UserRole).toULongLong();
    const QSize comp = m_engine->compositionSize();
    const QString text = cell->text().trimmed();
    bool ok = true;
    const double v = text.toDouble(&ok);
    if (column != FName && column != FOn && !ok) {
        m_refreshTimer->start(); // back to the value it had
        return;
    }
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Edit Fixture"), [&](FixturePatch &p) {
        for (Fixture &f : p.fixtures) {
            if (f.id != id) continue;
            switch (column) {
            case FOn: f.enabled = cell->checkState() == Qt::Checked; break;
            case FName: f.name = text; break;
            case FUniverse: f.universe = int(v); break;
            case FAddress: f.address = int(v); break;
            case FX: f.center.setX(v / std::max(1, comp.width())); break;
            case FY: f.center.setY(v / std::max(1, comp.height())); break;
            case FW: f.size.setWidth(v / std::max(1, comp.width())); break;
            case FH: f.size.setHeight(v / std::max(1, comp.height())); break;
            case FRot: f.rotation = v; break;
            case FIntensity: f.intensity = v / 100.0; break;
            default: break;
            }
        }
    });
    m_refreshTimer->start(); // a clamped value shown as it is
    emit projectEdited();
}

void FixturePanel::editOutputCell(int row, int column)
{
    const QTableWidgetItem *name = m_outputs->item(row, OName), *cell = m_outputs->item(row, column);
    if (!name || !cell || column == OState) return;
    const quint64 id = name->data(Qt::UserRole).toULongLong();
    const QString text = cell->text().trimmed();
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Edit DMX Output"), [&](FixturePatch &p) {
        for (DmxOutput &o : p.outputs) {
            if (o.id != id) continue;
            switch (column) {
            case OOn: o.enabled = cell->checkState() == Qt::Checked; break;
            case OName: o.name = text; break;
            case ODestination: o.destination = text; break;
            case OInterface: o.interfaceAddress = text; break;
            case ORate: o.rate = text.toDouble() > 0 ? text.toDouble() : o.rate; break;
            default: break;
            }
        }
    });
    m_refreshTimer->start();
    emit projectEdited();
}

void FixturePanel::addOutput()
{
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("New DMX Output"), [&](FixturePatch &p) {
        DmxOutput o;
        o.name = QStringLiteral("Art-Net %1").arg(p.outputs.size() + 1);
        p.outputs.push_back(o);
    });
    emit projectEdited();
}

void FixturePanel::removeOutput()
{
    const int row = m_outputs->currentRow();
    const QTableWidgetItem *name = row >= 0 ? m_outputs->item(row, OName) : nullptr;
    if (!name) return;
    const quint64 id = name->data(Qt::UserRole).toULongLong();
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Delete DMX Output"), [&](FixturePatch &p) {
        p.outputs.erase(std::remove_if(p.outputs.begin(), p.outputs.end(), [&](const DmxOutput &o) { return o.id == id; }),
                        p.outputs.end());
        for (Fixture &f : p.fixtures)
            if (f.output == id) f.output = 0;
    });
    emit projectEdited();
}

void FixturePanel::addFixture()
{
    const QString typeId = m_newType->currentData().toString();
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
        f.id = 0;
        for (const Fixture &x : p.fixtures) made = std::max(made, x.id);
        f.id = ++made;
        p.fixtures.push_back(f);
    });
    if (made) {
        refresh();
        selectFixture(made);
        emit fixtureSelected(made);
    }
    emit projectEdited();
}

void FixturePanel::duplicateFixture()
{
    std::set<quint64> ids;
    for (const QModelIndex &i : m_fixtures->selectionModel()->selectedRows())
        if (const QTableWidgetItem *n = m_fixtures->item(i.row(), FName)) ids.insert(n->data(Qt::UserRole).toULongLong());
    if (ids.empty()) return;
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Duplicate Fixtures"), [&](FixturePatch &p) {
        quint64 next = 0;
        for (const Fixture &x : p.fixtures) next = std::max(next, x.id);
        const std::vector<Fixture> source = p.fixtures;
        for (const Fixture &f : source) {
            if (!ids.count(f.id)) continue;
            Fixture copy = f;
            copy.id = ++next;
            copy.center += QPointF(0, f.size.height() * 1.5); // just under it
            if (const FixtureType *t = p.findType(f.type))
                std::tie(copy.universe, copy.address) = nextFree(p, f.output, t->channelCount());
            p.fixtures.push_back(copy);
        }
    });
    emit projectEdited();
}

void FixturePanel::removeFixtures()
{
    std::set<quint64> ids;
    for (const QModelIndex &i : m_fixtures->selectionModel()->selectedRows())
        if (const QTableWidgetItem *n = m_fixtures->item(i.row(), FName)) ids.insert(n->data(Qt::UserRole).toULongLong());
    if (ids.empty()) return;
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Delete Fixtures"), [&](FixturePatch &p) {
        p.fixtures.erase(std::remove_if(p.fixtures.begin(), p.fixtures.end(), [&](const Fixture &f) { return ids.count(f.id) > 0; }),
                         p.fixtures.end());
    });
    emit fixtureSelected(0);
    emit projectEdited();
}

void FixturePanel::removeType()
{
    const QListWidgetItem *i = m_types->currentItem();
    if (!i) return;
    const QString id = i->data(Qt::UserRole).toString();
    const FixturePatch p = m_engine->fixturePatch();
    if (std::any_of(p.fixtures.begin(), p.fixtures.end(), [&](const Fixture &f) { return f.type == id; })) {
        QMessageBox::information(this, QStringLiteral("Fixture type"), QStringLiteral("Fixtures use this type: change or delete them first."));
        return;
    }
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Delete Fixture Type"), [&](FixturePatch &q) {
        q.types.erase(std::remove_if(q.types.begin(), q.types.end(), [&](const FixtureType &t) { return t.id == id; }), q.types.end());
    });
    emit projectEdited();
}

QString FixturePanel::importGdtf(const QStringList &paths)
{
    std::vector<FixtureType> types;
    QStringList report;
    for (const QString &path : paths) {
        const gdtf::Import im = gdtf::readFile(path);
        if (!im.error.isEmpty()) report << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), im.error);
        for (const FixtureType &t : im.types) report << QStringLiteral("Imported %1 (%2 px, %3 channels)").arg(t.name).arg(t.pixelCount()).arg(t.channelCount());
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
        refresh();
        const int at = m_newType->findData(types.front().id);
        if (at >= 0) m_newType->setCurrentIndex(at);
        emit projectEdited();
    }
    return report.join(QLatin1Char('\n'));
}
