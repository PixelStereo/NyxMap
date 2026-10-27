#include "DmxOutputsPanel.h"
#include "Commands.h"
#include "Engine.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
enum Column { On, Name, Protocol, Destination, Interface, Rate, State, Count };

QTableWidgetItem *item(const QString &text, bool editable = true)
{
    auto *i = new QTableWidgetItem(text);
    if (!editable) i->setFlags(i->flags() & ~Qt::ItemIsEditable);
    return i;
}
} // namespace

DmxOutputsPanel::DmxOutputsPanel(Engine *engine, QUndoStack *undo, QWidget *parent)
    : QGroupBox(QStringLiteral("DMX outputs"), parent), m_engine(engine), m_undo(undo)
{
    setObjectName(QStringLiteral("dmxOutputs"));
    auto *v = new QVBoxLayout(this);
    auto *bar = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("+"));
    add->setObjectName(QStringLiteral("dmxOutputsAdd"));
    add->setToolTip(QStringLiteral("New DMX output (Art-Net or sACN)"));
    auto *remove = new QPushButton(QStringLiteral("−"));
    remove->setToolTip(QStringLiteral("Delete the selected output (its fixtures are left without output)"));
    for (auto *b : {add, remove}) b->setFixedWidth(30);
    bar->addWidget(add);
    bar->addWidget(remove);
    bar->addStretch(1);
    v->addLayout(bar);

    m_table = new QTableWidget(0, Count);
    m_table->setObjectName(QStringLiteral("dmxOutputsTable"));
    m_table->setHorizontalHeaderLabels({QStringLiteral("On"), QStringLiteral("Name"), QStringLiteral("Protocol"),
                                        QStringLiteral("Destination"), QStringLiteral("Interface"), QStringLiteral("Rate"),
                                        QStringLiteral("State")});
    m_table->horizontalHeaderItem(Destination)->setToolTip(QStringLiteral("An IP address; empty: broadcast (Art-Net) or multicast (sACN)"));
    QStringList locals;
    for (const QHostAddress &a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol) locals << a.toString();
    m_table->horizontalHeaderItem(Interface)->setToolTip(
        QStringLiteral("The local address sent from (its network card); empty: any.\nThis machine: %1").arg(locals.join(QStringLiteral(", "))));
    m_table->horizontalHeaderItem(Rate)->setToolTip(QStringLiteral("Packets per second and universe"));
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumHeight(90);
    m_table->setMaximumHeight(150);
    v->addWidget(m_table);
    auto *note = new QLabel(QStringLiteral("Universes: Art-Net from 0 (port address), sACN from 1. The fixtures are patched "
                                           "in the Fixtures tab."));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#888; font-size:11px;"));
    v->addWidget(note);

    connect(add, &QPushButton::clicked, this, [this] {
        cmd::editFixtures(m_undo, m_engine, QStringLiteral("New DMX Output"), [](FixturePatch &p) {
            DmxOutput o;
            o.name = QStringLiteral("Art-Net %1").arg(p.outputs.size() + 1);
            p.outputs.push_back(o);
        });
        emit projectEdited();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        const QTableWidgetItem *name = row >= 0 ? m_table->item(row, Name) : nullptr;
        if (!name) return;
        const quint64 id = name->data(Qt::UserRole).toULongLong();
        cmd::editFixtures(m_undo, m_engine, QStringLiteral("Delete DMX Output"), [&](FixturePatch &p) {
            p.outputs.erase(std::remove_if(p.outputs.begin(), p.outputs.end(), [&](const DmxOutput &o) { return o.id == id; }),
                            p.outputs.end());
            for (Fixture &f : p.fixtures)
                if (f.output == id) f.output = 0;
        });
        emit projectEdited();
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *i) {
        if (!m_updating) editCell(i->row(), i->column());
    });

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(60);
    connect(m_refreshTimer, &QTimer::timeout, this, &DmxOutputsPanel::refresh);
    connect(m_engine, &Engine::fixturesChanged, m_refreshTimer, qOverload<>(&QTimer::start));
    m_stateTimer = new QTimer(this);
    m_stateTimer->setInterval(1000);
    connect(m_stateTimer, &QTimer::timeout, this, &DmxOutputsPanel::refreshStates);
    m_stateTimer->start();
    refresh();
}

void DmxOutputsPanel::refresh()
{
    // Not under an edit in progress (a line edit in a cell has the focus): tried again a moment later
    if (const QWidget *f = QApplication::focusWidget(); f && m_table->viewport()->isAncestorOf(f) && qobject_cast<const QLineEdit *>(f)) {
        m_refreshTimer->start();
        return;
    }
    const FixturePatch p = m_engine->fixturePatch();
    const int keep = m_table->currentRow();
    m_updating = true;
    m_table->setRowCount(0);
    m_table->setRowCount(int(p.outputs.size()));
    for (int r = 0; r < int(p.outputs.size()); ++r) {
        const DmxOutput &o = p.outputs[size_t(r)];
        auto *on = new QTableWidgetItem;
        on->setFlags((on->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        on->setCheckState(o.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(r, On, on);
        auto *name = item(o.name.isEmpty() ? QStringLiteral("Output %1").arg(o.id) : o.name);
        name->setData(Qt::UserRole, o.id);
        m_table->setItem(r, Name, name);
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
        m_table->setCellWidget(r, Protocol, proto);
        m_table->setItem(r, Destination, item(o.destination));
        m_table->setItem(r, Interface, item(o.interfaceAddress));
        m_table->setItem(r, Rate, item(QString::number(o.rate, 'f', 0)));
        m_table->setItem(r, State, item(QString(), false));
    }
    if (keep >= 0 && keep < m_table->rowCount()) m_table->selectRow(keep);
    m_table->resizeColumnsToContents();
    m_updating = false;
    refreshStates();
}

void DmxOutputsPanel::refreshStates()
{
    if (!isVisible()) return;
    m_updating = true;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *name = m_table->item(r, Name), *state = m_table->item(r, State);
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

void DmxOutputsPanel::editCell(int row, int column)
{
    const QTableWidgetItem *name = m_table->item(row, Name), *cell = m_table->item(row, column);
    if (!name || !cell || column == State) return;
    const quint64 id = name->data(Qt::UserRole).toULongLong();
    const QString text = cell->text().trimmed();
    cmd::editFixtures(m_undo, m_engine, QStringLiteral("Edit DMX Output"), [&](FixturePatch &p) {
        for (DmxOutput &o : p.outputs) {
            if (o.id != id) continue;
            switch (column) {
            case On: o.enabled = cell->checkState() == Qt::Checked; break;
            case Name: o.name = text; break;
            case Destination: o.destination = text; break;
            case Interface: o.interfaceAddress = text; break;
            case Rate: o.rate = text.toDouble() > 0 ? text.toDouble() : o.rate; break;
            default: break;
            }
        }
    });
    m_refreshTimer->start();
    emit projectEdited();
}
