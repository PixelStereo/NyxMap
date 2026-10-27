#include "DmxOutputsPanel.h"
#include "Commands.h"
#include "Engine.h"
#include "Widgets.h"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
enum Column { On, Name, Protocol, State, Count };

QString outputName(const DmxOutput &o) { return o.name.isEmpty() ? QStringLiteral("Output %1").arg(o.id) : o.name; }

// The IP addresses typed: separated by commas, spaces or semicolons
QStringList parseAddresses(const QString &text)
{
    QStringList out;
    for (const QString &part : text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts)) out << part;
    return out;
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
    m_table->setHorizontalHeaderLabels({QStringLiteral("On"), QStringLiteral("Name"), QStringLiteral("Protocol"), QStringLiteral("State")});
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumHeight(80);
    m_table->setMaximumHeight(130);
    v->addWidget(m_table);

    // --- The selected output's settings
    m_editor = new QWidget;
    m_form = new QFormLayout(m_editor);
    m_form->setContentsMargins(0, 4, 0, 0);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_interface = new QComboBox;
    m_interface->setObjectName(QStringLiteral("dmxOutputInterface"));
    m_interface->setToolTip(QStringLiteral("The network card it sends from"));
    m_form->addRow(QStringLiteral("Interface"), m_interface);
    m_mode = new QComboBox;
    m_mode->setObjectName(QStringLiteral("dmxOutputMode"));
    m_form->addRow(QStringLiteral("Mode"), m_mode);
    m_addresses = new QLineEdit;
    m_addresses->setObjectName(QStringLiteral("dmxOutputAddresses"));
    m_addresses->setPlaceholderText(QStringLiteral("2.0.0.10, 2.0.0.11"));
    m_addresses->setToolTip(QStringLiteral("One or more IP addresses, separated by commas"));
    m_form->addRow(QStringLiteral("IP addresses"), m_addresses);
    m_priority = new IntBox;
    m_priority->setObjectName(QStringLiteral("dmxOutputPriority"));
    m_priority->setRange(0, 200);
    m_priority->setKeyboardTracking(false);
    m_priority->setToolTip(QStringLiteral("sACN priority: a receiver takes the highest of the sources of a universe (100 by default)"));
    m_form->addRow(QStringLiteral("Priority"), m_priority);
    m_sourceName = new QLineEdit;
    m_sourceName->setObjectName(QStringLiteral("dmxOutputSourceName"));
    m_sourceName->setToolTip(QStringLiteral("The name receivers show for this source"));
    m_form->addRow(QStringLiteral("Source name"), m_sourceName);
    m_rate = new IntBox;
    m_rate->setObjectName(QStringLiteral("dmxOutputRate"));
    m_rate->setRange(1, 120);
    m_rate->setSuffix(QStringLiteral(" Hz"));
    m_rate->setKeyboardTracking(false);
    m_rate->setToolTip(QStringLiteral("Packets per second and universe"));
    m_form->addRow(QStringLiteral("Rate"), m_rate);
    v->addWidget(m_editor);
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
        m_table->selectRow(m_table->rowCount() - 1);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const quint64 id = selectedOutput();
        if (!id) return;
        cmd::editFixtures(m_undo, m_engine, QStringLiteral("Delete DMX Output"), [&](FixturePatch &p) {
            p.outputs.erase(std::remove_if(p.outputs.begin(), p.outputs.end(), [&](const DmxOutput &o) { return o.id == id; }),
                            p.outputs.end());
            for (Fixture &f : p.fixtures)
                if (f.output == id) f.output = 0;
        });
        emit projectEdited();
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *i) {
        if (m_updating) return;
        const quint64 id = m_table->item(i->row(), Name)->data(Qt::UserRole).toULongLong();
        if (i->column() == On) {
            const bool on = i->checkState() == Qt::Checked;
            cmd::editFixtures(m_undo, m_engine, QStringLiteral("Edit DMX Output"), [&](FixturePatch &p) {
                for (DmxOutput &o : p.outputs)
                    if (o.id == id) o.enabled = on;
            });
        } else if (i->column() == Name) {
            const QString name = i->text().trimmed();
            cmd::editFixtures(m_undo, m_engine, QStringLiteral("Rename DMX Output"), [&](FixturePatch &p) {
                for (DmxOutput &o : p.outputs)
                    if (o.id == id) o.name = name;
            });
        } else {
            return;
        }
        m_refreshTimer->start();
        emit projectEdited();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &DmxOutputsPanel::refreshEditor);

    connect(m_interface, &QComboBox::activated, this, [this] {
        const QString ip = m_interface->currentData().toString();
        editOutput(QStringLiteral("Output Interface"), {}, [&](DmxOutput &o) { o.interfaceAddress = ip; });
    });
    connect(m_mode, &QComboBox::activated, this, [this](int i) {
        editOutput(QStringLiteral("Output Mode"), {}, [&](DmxOutput &o) { o.unicast = i == 1; });
    });
    connect(m_addresses, &QLineEdit::editingFinished, this, [this] {
        const QStringList ips = parseAddresses(m_addresses->text());
        editOutput(QStringLiteral("Output Addresses"), {}, [&](DmxOutput &o) { o.destinations = ips; });
    });
    connect(m_priority, &QSpinBox::valueChanged, this, [this](int v) {
        editOutput(QStringLiteral("Output Priority"), QStringLiteral("priority"), [&](DmxOutput &o) { o.priority = v; });
    });
    connect(m_sourceName, &QLineEdit::editingFinished, this, [this] {
        const QString name = m_sourceName->text().trimmed();
        editOutput(QStringLiteral("Output Source Name"), {}, [&](DmxOutput &o) { o.sourceName = name.isEmpty() ? QStringLiteral("Pinol") : name; });
    });
    connect(m_rate, &QSpinBox::valueChanged, this, [this](int v) {
        editOutput(QStringLiteral("Output Rate"), QStringLiteral("rate"), [&](DmxOutput &o) { o.rate = v; });
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

quint64 DmxOutputsPanel::selectedOutput() const
{
    const int row = m_table->currentRow();
    const QTableWidgetItem *name = row >= 0 ? m_table->item(row, Name) : nullptr;
    return name ? name->data(Qt::UserRole).toULongLong() : 0;
}

void DmxOutputsPanel::editOutput(const QString &text, const QString &field, const std::function<void(DmxOutput &)> &fn)
{
    const quint64 id = selectedOutput();
    if (m_updating || !id) return;
    cmd::editFixtures(m_undo, m_engine, text, [&](FixturePatch &p) {
        for (DmxOutput &o : p.outputs)
            if (o.id == id) fn(o);
    }, field.isEmpty() ? QString() : QStringLiteral("output-%1-%2").arg(field).arg(id));
    emit projectEdited();
}

void DmxOutputsPanel::setRowShown(QWidget *field, bool shown)
{
    field->setVisible(shown);
    if (QWidget *label = m_form->labelForField(field)) label->setVisible(shown);
}

void DmxOutputsPanel::refresh()
{
    // Not under a rename in progress (a line edit in a cell has the focus): tried again a moment later
    if (const QWidget *f = QApplication::focusWidget(); f && m_table->viewport()->isAncestorOf(f) && qobject_cast<const QLineEdit *>(f)) {
        m_refreshTimer->start();
        return;
    }
    const FixturePatch p = m_engine->fixturePatch();
    const quint64 keep = selectedOutput();
    m_updating = true;
    m_table->setRowCount(0);
    m_table->setRowCount(int(p.outputs.size()));
    int keepRow = p.outputs.empty() ? -1 : 0;
    for (int r = 0; r < int(p.outputs.size()); ++r) {
        const DmxOutput &o = p.outputs[size_t(r)];
        if (o.id == keep) keepRow = r;
        auto *on = new QTableWidgetItem;
        on->setFlags((on->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        on->setCheckState(o.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(r, On, on);
        auto *name = new QTableWidgetItem(outputName(o));
        name->setData(Qt::UserRole, o.id);
        m_table->setItem(r, Name, name);
        auto *proto = new QComboBox;
        proto->addItems({QStringLiteral("Art-Net"), QStringLiteral("sACN")});
        proto->setCurrentIndex(o.protocol == DmxOutput::Protocol::Sacn ? 1 : 0);
        const quint64 id = o.id;
        connect(proto, &QComboBox::activated, this, [this, id](int index) {
            cmd::editFixtures(m_undo, m_engine, QStringLiteral("Output Protocol"), [&](FixturePatch &q) {
                for (DmxOutput &x : q.outputs)
                    if (x.id == id) x.protocol = index == 1 ? DmxOutput::Protocol::Sacn : DmxOutput::Protocol::ArtNet;
            });
            emit projectEdited();
        });
        m_table->setCellWidget(r, Protocol, proto);
        auto *state = new QTableWidgetItem;
        state->setFlags(state->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(r, State, state);
    }
    if (keepRow >= 0) m_table->selectRow(keepRow);
    m_table->resizeColumnsToContents();
    m_updating = false;
    refreshEditor();
    refreshStates();
}

void DmxOutputsPanel::refreshEditor()
{
    const FixturePatch p = m_engine->fixturePatch();
    const DmxOutput *o = p.findOutput(selectedOutput());
    m_editor->setVisible(o != nullptr);
    if (!o) return;
    const bool sacn = o->protocol == DmxOutput::Protocol::Sacn;
    m_updating = true;
    QSignalBlocker b1(m_interface), b2(m_mode), b3(m_priority), b4(m_rate);

    // This machine's network cards (up, with an IPv4 address), and the one chosen if it is not here
    m_interface->clear();
    m_interface->addItem(QStringLiteral("Any (the system's choice)"), QString());
    for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp)) continue;
        for (const QNetworkAddressEntry &e : i.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                m_interface->addItem(QStringLiteral("%1 · %2").arg(i.humanReadableName(), e.ip().toString()), e.ip().toString());
    }
    int at = m_interface->findData(o->interfaceAddress);
    if (at < 0) {
        m_interface->addItem(QStringLiteral("%1 (not on this machine)").arg(o->interfaceAddress), o->interfaceAddress);
        at = m_interface->count() - 1;
    }
    m_interface->setCurrentIndex(at);

    m_mode->clear();
    m_mode->addItems({sacn ? QStringLiteral("Multicast") : QStringLiteral("Broadcast"), QStringLiteral("Unicast")});
    m_mode->setCurrentIndex(o->unicast ? 1 : 0);
    m_mode->setToolTip(sacn ? QStringLiteral("Multicast: each universe to its group (239.255.x.y); unicast: to the IP addresses given")
                            : QStringLiteral("Broadcast: to every node of the interface's network; unicast: to the IP addresses given"));
    if (!m_addresses->hasFocus()) m_addresses->setText(o->destinations.join(QStringLiteral(", ")));
    QStringList bad;
    for (const QString &ip : o->destinations)
        if (QHostAddress(ip).isNull()) bad << ip;
    m_addresses->setStyleSheet(bad.isEmpty() && !(o->unicast && o->destinations.isEmpty())
                                   ? QString() : QStringLiteral("QLineEdit { border: 1px solid #c55; }"));
    m_addresses->setToolTip(!bad.isEmpty() ? QStringLiteral("Not IP addresses: %1").arg(bad.join(QStringLiteral(", ")))
                                           : QStringLiteral("One or more IP addresses, separated by commas"));
    m_priority->setValue(o->priority);
    if (!m_sourceName->hasFocus()) m_sourceName->setText(o->sourceName);
    m_rate->setValue(int(std::lround(o->rate)));
    setRowShown(m_addresses, o->unicast);
    setRowShown(m_priority, sacn);
    setRowShown(m_sourceName, sacn);
    m_updating = false;
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
                       : s.level == DmxOutputState::Error ? QStringLiteral("Error: %1").arg(s.text)
                                                          : QStringLiteral("Sending · %1 packets").arg(s.packets));
        state->setToolTip(s.text);
        state->setForeground(s.level == DmxOutputState::Error ? QColor(230, 90, 90) : palette().color(QPalette::Text));
    }
    m_updating = false;
}
