#include "FixtureInspector.h"
#include "Commands.h"
#include "Engine.h"
#include "FixtureOverlay.h"
#include "InteractionPanel.h"
#include "PresetEditor.h"
#include "TimelinePanel.h"
#include "Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QUuid>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QHeaderView>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>
#include <memory>
#include <vector>

FixtureType templateOf(const FixtureType &type, const QString &name)
{
    FixtureType t = type;
    t.id = QStringLiteral("template-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(10));
    t.name = name;
    t.builtIn = false;
    return t;
}

FixtureInspector::FixtureInspector(Engine *engine, QUndoStack *undo, QWidget *parent)
    : QWidget(parent), m_engine(engine), m_undo(undo)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(10);
    m_empty = new QLabel(QStringLiteral("No fixture selected: pick one in the Fixtures list or in the view "
                                        "(its Fixtures button on)."));
    m_empty->setWordWrap(true);
    m_empty->setStyleSheet(QStringLiteral("color:#888;"));
    v->addWidget(m_empty);
    m_form = new QWidget;
    auto *fv = new QVBoxLayout(m_form);
    fv->setContentsMargins(0, 0, 0, 0);
    fv->setSpacing(10);
    v->addWidget(m_form);
    v->addStretch(1);

    auto group = [&](const QString &title) {
        auto *g = new QGroupBox(title);
        auto *form = new QFormLayout(g);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        fv->addWidget(g);
        return form;
    };
    auto number = [](const char *name, double min, double max, int decimals, const QString &suffix) {
        auto *b = new NumberBox;
        b->setObjectName(QLatin1String(name));
        b->setRange(min, max);
        b->setDecimals(decimals);
        b->setSuffix(suffix);
        b->setKeyboardTracking(false);
        return b;
    };
    auto integer = [](const char *name, int min, int max) {
        auto *b = new IntBox;
        b->setObjectName(QLatin1String(name));
        b->setRange(min, max);
        b->setKeyboardTracking(false);
        return b;
    };

    // --- Fixture: name, type, pixels
    QFormLayout *fx = group(QStringLiteral("Fixture"));
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("fixtureName"));
    fx->addRow(QStringLiteral("Name"), m_name);
    m_type = new QComboBox;
    m_type->setObjectName(QStringLiteral("fixtureType"));
    fx->addRow(QStringLiteral("Type"), m_type);
    m_typeInfo = new QLabel;
    m_typeInfo->setStyleSheet(QStringLiteral("color:#888; font-size:11px;"));
    m_typeInfo->setWordWrap(true);
    fx->addRow(QString(), m_typeInfo);
    // Its type as it is here (its own channels included), kept on this machine: offered in every project
    auto *keep = new QToolButton;
    keep->setObjectName(QStringLiteral("fixtureSaveTemplate"));
    keep->setText(QStringLiteral("Save as Template…"));
    keep->setToolTip(QStringLiteral("Keep this fixture's type, with its channels as set here, as a template offered in every project"));
    fx->addRow(QString(), keep);
    connect(keep, &QToolButton::clicked, this, [this] {
        const FixturePatch p = m_engine->fixturePatch();
        const Fixture *f = p.findFixture(m_id);
        const std::optional<FixtureType> t = f ? p.resolvedType(*f) : std::nullopt;
        if (!t) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Save as Template"), QStringLiteral("Name"), QLineEdit::Normal, t->name, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        QString why;
        if (!saveFixtureTemplate(templateOf(*t, name), &why))
            QMessageBox::warning(this, QStringLiteral("Save as Template"), QStringLiteral("Not saved: %1").arg(why));
    });
    auto *grid = new QHBoxLayout;
    m_cols = integer("fixtureCols", 1, 1024);
    m_rows = integer("fixtureRows", 1, 1024);
    grid->addWidget(m_cols);
    grid->addWidget(new QLabel(QStringLiteral("×")));
    grid->addWidget(m_rows);
    grid->addStretch(1);
    fx->addRow(QStringLiteral("Pixels"), grid);
    m_order = new QComboBox;
    m_order->addItems({QStringLiteral("Row after row"), QStringLiteral("Column after column")});
    m_start = new QComboBox;
    m_start->addItems({QStringLiteral("Top left"), QStringLiteral("Top right"), QStringLiteral("Bottom left"), QStringLiteral("Bottom right")});
    m_snake = new QCheckBox(QStringLiteral("Snake (every other line goes back)"));
    fx->addRow(QStringLiteral("Order"), m_order);
    fx->addRow(QStringLiteral("Starts"), m_start);
    fx->addRow(QString(), m_snake);

    // --- DMX
    QFormLayout *dmx = group(QStringLiteral("DMX"));
    m_output = new QComboBox;
    m_output->setObjectName(QStringLiteral("fixtureOutput"));
    dmx->addRow(QStringLiteral("Output"), m_output);
    m_universe = integer("fixtureUniverse", 0, 63999);
    m_universe->setToolTip(QStringLiteral("Art-Net: port address from 0; sACN: 1..63999"));
    dmx->addRow(QStringLiteral("Universe"), m_universe);
    m_address = integer("fixtureAddress", 1, 512);
    dmx->addRow(QStringLiteral("Address"), m_address);
    m_dmx = new QLabel;
    m_dmx->setObjectName(QStringLiteral("fixtureChannels"));
    dmx->addRow(QStringLiteral("Channels"), m_dmx);

    // --- Place, in composition pixels
    QFormLayout *place = group(QStringLiteral("Place"));
    m_x = number("fixtureX", -100000, 100000, 1, QStringLiteral(" px"));
    m_y = number("fixtureY", -100000, 100000, 1, QStringLiteral(" px"));
    m_w = number("fixtureW", 0, 100000, 1, QStringLiteral(" px"));
    m_h = number("fixtureH", 0, 100000, 1, QStringLiteral(" px"));
    m_rotation = number("fixtureRotation", -360, 360, 1, QStringLiteral("°"));
    auto pair = [](QWidget *a, QWidget *b) {
        auto *h = new QHBoxLayout;
        h->addWidget(a);
        h->addWidget(b);
        return h;
    };
    place->addRow(QStringLiteral("Center"), pair(m_x, m_y));
    place->addRow(QStringLiteral("Size"), pair(m_w, m_h));
    place->addRow(QStringLiteral("Rotation"), m_rotation);

    // --- Output: intensity, gamma, white
    QFormLayout *out = group(QStringLiteral("Output"));
    m_intensity = new SliderField;
    m_intensity->setObjectName(QStringLiteral("fixtureInspectorIntensity"));
    m_intensity->setRange(0, 100);
    m_intensity->setDecimals(0);
    m_intensity->setSuffix(QStringLiteral(" %"));
    m_intensity->setSnaps({100});
    m_intensity->setToolTip(QStringLiteral("A factor on everything the fixture sends"));
    out->addRow(QStringLiteral("Intensity"), m_intensity);
    m_gamma = number("fixtureGamma", 0.1, 10, 2, QString());
    m_gamma->setSingleStep(0.1);
    m_gamma->setToolTip(QStringLiteral("On the sampled colour: 1 linear, above 1 darker mid-tones"));
    out->addRow(QStringLiteral("Gamma"), m_gamma);
    m_white = new QComboBox;
    m_white->addItems({QStringLiteral("From the colour (min of R, G, B)"), QStringLiteral("Off")});
    m_white->setToolTip(QStringLiteral("A white channel (RGBW): the grey part of the colour moved to it, or left at 0"));
    out->addRow(QStringLiteral("White"), m_white);

    // --- Channels: each one follows the pixel mapping or sends a fixed value; channels added after its own. A fixed
    // value is a value of the show too ("Channel 5" of the fixture): snapshots, presets, timelines and controls set it
    QFormLayout *channelsForm = group(QStringLiteral("Channels"));
    m_channels = new ChannelEditor;
    channelsForm->addRow(m_channels);
    connect(m_channels, &ChannelEditor::edited, this, [this] {
        const std::map<int, FixtureChannel> own = ChannelEditor::ownChannels(m_channels->rows());
        edit(QStringLiteral("Fixture Channels"), QStringLiteral("channels"), [&](Fixture &f, const FixturePatch &) { f.channels = own; });
    });

    // --- Edits
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        const QString name = m_name->text().trimmed();
        edit(QStringLiteral("Rename Fixture"), {}, [&](Fixture &f, const FixturePatch &) { f.name = name; });
    });
    connect(m_type, &QComboBox::currentIndexChanged, this, [this] {
        const QString type = m_type->currentData().toString();
        edit(QStringLiteral("Fixture Type"), {}, [&](Fixture &f, const FixturePatch &) { f.type = type; });
    });
    connect(m_cols, &QSpinBox::valueChanged, this, [this](int n) {
        edit(QStringLiteral("Fixture Pixels"), QStringLiteral("cols"), [&](Fixture &f, const FixturePatch &) {
            f.size.setWidth(f.size.width() * n / std::max(1, f.cols)); // the pixels keep their size
            f.cols = n;
        });
    });
    connect(m_rows, &QSpinBox::valueChanged, this, [this](int n) {
        edit(QStringLiteral("Fixture Pixels"), QStringLiteral("rows"), [&](Fixture &f, const FixturePatch &) {
            f.size.setHeight(f.size.height() * n / std::max(1, f.rows));
            f.rows = n;
        });
    });
    connect(m_order, &QComboBox::currentIndexChanged, this, [this](int i) {
        edit(QStringLiteral("Pixel Order"), {}, [&](Fixture &f, const FixturePatch &) { f.byColumns = i == 1; });
    });
    connect(m_start, &QComboBox::currentIndexChanged, this, [this](int i) {
        edit(QStringLiteral("Pixel Order"), {}, [&](Fixture &f, const FixturePatch &) { f.start = FixtureType::Start(i); });
    });
    connect(m_snake, &QCheckBox::toggled, this, [this](bool on) {
        edit(QStringLiteral("Pixel Order"), {}, [&](Fixture &f, const FixturePatch &) { f.snake = on; });
    });
    connect(m_output, &QComboBox::currentIndexChanged, this, [this] {
        const quint64 out = m_output->currentData().toULongLong();
        edit(QStringLiteral("Fixture Output"), {}, [&](Fixture &f, const FixturePatch &) { f.output = out; });
    });
    connect(m_universe, &QSpinBox::valueChanged, this, [this](int u) {
        edit(QStringLiteral("Fixture Universe"), QStringLiteral("universe"), [&](Fixture &f, const FixturePatch &) { f.universe = u; });
    });
    connect(m_address, &QSpinBox::valueChanged, this, [this](int a) {
        edit(QStringLiteral("Fixture Address"), QStringLiteral("address"), [&](Fixture &f, const FixturePatch &) { f.address = a; });
    });
    auto placeEdit = [this](NumberBox *box, const char *field, std::function<void(Fixture &, double, QSize)> set) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this, field, set](double value) {
            const QSize comp = m_engine->compositionSize();
            edit(QStringLiteral("Place Fixture"), QLatin1String(field), [&](Fixture &f, const FixturePatch &) {
                if (!f.locked) set(f, value, comp);
            });
        });
    };
    placeEdit(m_x, "x", [](Fixture &f, double v, QSize c) { f.center.setX(v / std::max(1, c.width())); });
    placeEdit(m_y, "y", [](Fixture &f, double v, QSize c) { f.center.setY(v / std::max(1, c.height())); });
    placeEdit(m_w, "w", [](Fixture &f, double v, QSize c) { f.size.setWidth(v / std::max(1, c.width())); });
    placeEdit(m_h, "h", [](Fixture &f, double v, QSize c) { f.size.setHeight(v / std::max(1, c.height())); });
    placeEdit(m_rotation, "rotation", [](Fixture &f, double v, QSize) { f.rotation = v; });
    connect(m_intensity, &SliderField::valueEdited, this, [this](double v) {
        edit(QStringLiteral("Fixture Intensity"), QStringLiteral("intensity"), [&](Fixture &f, const FixturePatch &) { f.intensity = v / 100.0; });
    });
    connect(m_gamma, &QDoubleSpinBox::valueChanged, this, [this](double g) {
        edit(QStringLiteral("Fixture Gamma"), QStringLiteral("gamma"), [&](Fixture &f, const FixturePatch &) { f.gamma = g; });
    });
    connect(m_white, &QComboBox::currentIndexChanged, this, [this](int i) {
        edit(QStringLiteral("Fixture White"), {}, [&](Fixture &f, const FixturePatch &) {
            f.white = i == 1 ? Fixture::White::Off : Fixture::White::Min;
        });
    });

    connect(m_engine, &Engine::fixturesChanged, this, &FixtureInspector::refresh);
    connect(m_engine, &Engine::compositionSizeChanged, this, &FixtureInspector::refresh);
    refresh();
}

void FixtureInspector::setFixture(quint64 id)
{
    m_id = id;
    refresh();
}

void FixtureInspector::edit(const QString &text, const QString &field, const std::function<void(Fixture &, const FixturePatch &)> &fn)
{
    if (m_updating || !m_id) return;
    const quint64 id = m_id;
    cmd::editFixtures(m_undo, m_engine, text, [&](FixturePatch &p) {
        const FixturePatch before = p;
        for (Fixture &f : p.fixtures)
            if (f.id == id) fn(f, before);
    }, field.isEmpty() ? QString() : QStringLiteral("fixture-%1-%2").arg(field).arg(id));
    emit projectEdited();
}

void FixtureInspector::refresh()
{
    const FixturePatch p = m_engine->fixturePatch();
    const auto it = std::find_if(p.fixtures.begin(), p.fixtures.end(), [&](const Fixture &f) { return f.id == m_id; });
    const bool has = it != p.fixtures.end();
    m_empty->setVisible(!has);
    m_form->setVisible(has);
    if (!has) return;
    const Fixture &f = *it;
    const QSize comp = m_engine->compositionSize();
    m_updating = true;
    const QList<QWidget *> all{m_name, m_type, m_order, m_start, m_output, m_white, m_cols, m_rows, m_universe,
                               m_address, m_snake, m_x, m_y, m_w, m_h, m_rotation, m_gamma};
    std::vector<std::unique_ptr<QSignalBlocker>> blocks;
    for (QWidget *w : all) blocks.push_back(std::make_unique<QSignalBlocker>(w));

    if (!m_name->hasFocus()) m_name->setText(f.name);
    m_type->clear();
    for (const FixtureType &t : builtInFixtureTypes()) m_type->addItem(t.name, t.id);
    for (const FixtureType &t : p.types) m_type->addItem(t.name, t.id);
    if (m_type->findData(f.type) < 0) m_type->addItem(QStringLiteral("(missing: %1)").arg(f.type), f.type);
    m_type->setCurrentIndex(m_type->findData(f.type));
    const FixtureType *base = p.findType(f.type);
    const std::optional<FixtureType> t = p.resolvedType(f);
    const bool grid = base && takesFixtureGrid(*base);
    m_typeInfo->setText(!t ? QStringLiteral("This type is not in the project.")
                           : QStringLiteral("%1 px, %2 channels%3").arg(t->pixelCount()).arg(t->channelCount())
                                 .arg(grid ? QString() : QStringLiteral(" — the type sets its own pixels")));
    m_cols->setValue(grid ? f.cols : (t ? t->cols : 1));
    m_rows->setValue(grid ? f.rows : (t ? t->rows : 1));
    m_order->setCurrentIndex(f.byColumns ? 1 : 0);
    m_start->setCurrentIndex(int(f.start));
    m_snake->setChecked(f.snake);
    for (QWidget *w : {static_cast<QWidget *>(m_cols), static_cast<QWidget *>(m_rows), static_cast<QWidget *>(m_order),
                       static_cast<QWidget *>(m_start), static_cast<QWidget *>(m_snake)})
        w->setEnabled(grid);

    m_output->clear();
    m_output->addItem(QStringLiteral("—"), QVariant::fromValue<quint64>(0));
    for (const DmxOutput &o : p.outputs)
        m_output->addItem(o.name.isEmpty() ? QStringLiteral("Output %1").arg(o.id) : o.name, QVariant::fromValue(o.id));
    m_output->setCurrentIndex(std::max(0, m_output->findData(QVariant::fromValue(f.output))));
    const DmxOutput *out = p.findOutput(f.output);
    m_universe->setMinimum(out && out->protocol == pxs::dmx::Protocol::Sacn ? 1 : 0);
    m_universe->setValue(f.universe);
    m_address->setValue(f.address);
    m_dmx->setText(fixtureview::address(p, f));

    m_x->setValue(f.center.x() * comp.width());
    m_y->setValue(f.center.y() * comp.height());
    m_w->setValue(f.size.width() * comp.width());
    m_h->setValue(f.size.height() * comp.height());
    m_rotation->setValue(f.rotation);
    for (QWidget *w : {static_cast<QWidget *>(m_x), static_cast<QWidget *>(m_y), static_cast<QWidget *>(m_w),
                       static_cast<QWidget *>(m_h), static_cast<QWidget *>(m_rotation)})
        w->setEnabled(!f.locked);
    if (!m_intensity->isDragging()) m_intensity->setValue(std::round(f.intensity * 100));
    m_gamma->setValue(f.gamma);
    m_white->setCurrentIndex(f.white == Fixture::White::Off ? 1 : 0);
    m_channels->setOwner(m_engine, m_undo, f.id);
    m_channels->setRows(ChannelEditor::rowsOf(p, f));
    attachShowValue(m_intensity, m_engine, m_undo, {QStringLiteral("fixture/%1/intensity").arg(f.id)});
    m_updating = false;
}

// --- Channel editor

namespace {
// The roles offered, as the pixel mapping uses them, then a fixed value or nothing
const ChannelRole kRoles[] = {ChannelRole::Dimmer, ChannelRole::Red,  ChannelRole::Green,   ChannelRole::Blue,
                              ChannelRole::White,  ChannelRole::Amber, ChannelRole::Uv,     ChannelRole::Cyan,
                              ChannelRole::Magenta, ChannelRole::Yellow, ChannelRole::Fixed, ChannelRole::Ignore};
enum EditorColumn { ColChannel, ColPixel, ColRole, ColValue, ColReset, EditorColumns };

const QString kIgnoreTip = QStringLiteral(
    "Not NyxMap's: another device on the DMX network drives it.\n"
    "sACN: given away (a priority of 0 for this channel), for receivers and nodes that know priorities by channel.\n"
    "Art-Net: sent at 0; a node merging HTP lets the other device through.");

bool same(const FixtureChannel &a, const FixtureChannel &b)
{
    return a.role == b.role && (a.role != ChannelRole::Fixed || a.value == b.value);
}
} // namespace

ChannelEditor::ChannelEditor(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    m_table = new QTableWidget(0, EditorColumns);
    m_table->setObjectName(QStringLiteral("fixtureChannels"));
    m_table->setHorizontalHeaderLabels({QStringLiteral("Ch"), QStringLiteral("Px"), QStringLiteral("Role"), QStringLiteral("Value"), QString()});
    m_table->horizontalHeaderItem(ColRole)->setToolTip(QStringLiteral("What drives it: the pixel mapping (a colour, a dimmer), a fixed value, or nothing"));
    m_table->horizontalHeaderItem(ColValue)->setToolTip(QStringLiteral("A fixed channel's value, sent as it is (0..255)"));
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(28);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    auto *h = m_table->horizontalHeader();
    h->setHighlightSections(false);
    for (auto [col, width] : {std::pair{ColChannel, 34}, {ColPixel, 34}, {ColRole, 104}, {ColReset, 28}}) {
        h->setSectionResizeMode(col, QHeaderView::Fixed);
        m_table->setColumnWidth(col, width);
    }
    h->setSectionResizeMode(ColValue, QHeaderView::Stretch);
    m_table->setMinimumHeight(120);
    v->addWidget(m_table, 1);
    m_add = new QToolButton;
    m_add->setObjectName(QStringLiteral("fixtureAddChannel"));
    m_add->setText(QStringLiteral("+ Channel"));
    m_add->setToolTip(QStringLiteral("A fixed channel after the last: its value is sent as it is"));
    v->addWidget(m_add, 0, Qt::AlignLeft);
    connect(m_add, &QToolButton::clicked, this, [this] {
        int next = 1;
        for (const Row &r : m_rows) next = std::max(next, r.channel + r.now.width());
        m_rows.push_back({next, -1, FixtureChannel{ChannelRole::Fixed, false, 0}, std::nullopt});
        rebuild();
        emit edited();
    });
}

void ChannelEditor::setRows(const std::vector<Row> &rows)
{
    bool sameChannels = rows.size() == m_rows.size();
    for (size_t i = 0; sameChannels && i < rows.size(); ++i)
        sameChannels = rows[i].channel == m_rows[i].channel && rows[i].now.role == m_rows[i].now.role &&
                       rows[i].type.has_value() == m_rows[i].type.has_value();
    m_rows = rows;
    if (sameChannels && !m_ownerChanged) showValues();
    else rebuild();
    m_ownerChanged = false;
}

void ChannelEditor::setOwner(Engine *e, QUndoStack *undo, quint64 fixture)
{
    m_ownerChanged = m_ownerChanged || fixture != m_fixture;
    m_engine = e;
    m_undo = undo;
    m_fixture = fixture;
}

void attachShowValue(QWidget *w, Engine *e, QUndoStack *undo, const QStringList &paths)
{
    if (!w) return;
    w->setProperty("showValuePaths", paths);
    if (PresetEditor *pe = PresetEditor::instance()) pe->track(w, 0, paths);
    if (w->property("showValueMenu").toBool()) return; // its right-click is there already: it reads the paths above
    w->setProperty("showValueMenu", true);
    w->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(w, &QWidget::customContextMenuRequested, w, [w, e, undo](const QPoint &pos) {
        const QStringList now = w->property("showValuePaths").toStringList();
        auto *menu = new QMenu(w);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        if (PresetEditor *pe = PresetEditor::instance()) pe->fillMenu(menu, 0, now);
        TimelineWindow::fillParameterMenu(menu, e, undo, 0, now, w);
        InteractionPanel::fillValueMenu(menu, e, 0, now);
        if (menu->isEmpty()) {
            delete menu;
            return;
        }
        menu->popup(w->mapToGlobal(pos));
    });
}

void ChannelEditor::change(int index, const FixtureChannel &c)
{
    if (m_updating || index < 0 || index >= int(m_rows.size())) return;
    Row &r = m_rows[size_t(index)];
    const bool roleChanged = r.now.role != c.role;
    r.now.role = c.role;
    r.now.value = c.value;
    if (roleChanged) rebuild();
    else showValues();
    emit edited();
}

void ChannelEditor::rebuild()
{
    m_updating = true;
    m_table->clearContents();
    m_table->setRowCount(int(m_rows.size()));
    for (int i = 0; i < int(m_rows.size()); ++i) {
        const Row &r = m_rows[size_t(i)];
        auto *ch = new QTableWidgetItem(r.now.fine ? QStringLiteral("%1-%2").arg(r.channel).arg(r.channel + 1) : QString::number(r.channel));
        ch->setTextAlignment(Qt::AlignCenter);
        ch->setForeground(r.type ? QColor(200, 200, 205) : QColor(232, 195, 90));
        ch->setToolTip(r.type ? QStringLiteral("Channel %1 from the fixture's address").arg(r.channel)
                              : QStringLiteral("Added after the type's channels"));
        m_table->setItem(i, ColChannel, ch);
        auto *px = new QTableWidgetItem(r.pixel < 0 ? QStringLiteral("—") : QString::number(r.pixel + 1));
        px->setTextAlignment(Qt::AlignCenter);
        px->setForeground(QColor(140, 140, 150));
        px->setToolTip(r.pixel < 0 ? QStringLiteral("The whole fixture") : QStringLiteral("Pixel %1").arg(r.pixel + 1));
        m_table->setItem(i, ColPixel, px);

        auto *role = new QComboBox;
        role->setObjectName(QStringLiteral("channelRole%1").arg(r.channel));
        for (ChannelRole x : kRoles) {
            role->addItem(channelRoleName(x), int(x));
            if (x == ChannelRole::Fixed)
                role->setItemData(role->count() - 1, QStringLiteral("A value sent as it is (0..255)"), Qt::ToolTipRole);
            else if (x == ChannelRole::Ignore)
                role->setItemData(role->count() - 1, kIgnoreTip, Qt::ToolTipRole);
            else
                role->setItemData(role->count() - 1, QStringLiteral("Driven by the pixel mapping"), Qt::ToolTipRole);
        }
        role->setCurrentIndex(std::max(0, role->findData(int(r.now.role))));
        m_table->setCellWidget(i, ColRole, role);
        connect(role, &QComboBox::currentIndexChanged, this, [this, i, role] {
            FixtureChannel c = m_rows[size_t(i)].now;
            c.role = ChannelRole(role->currentData().toInt());
            QTimer::singleShot(0, this, [this, i, c] { change(i, c); }); // not while its own combo is being handled
        });

        if (r.now.role == ChannelRole::Fixed) {
            auto *value = new SliderField;
            value->setObjectName(QStringLiteral("fixtureChannel%1").arg(r.channel));
            value->setRange(0, 255);
            value->setDecimals(0);
            value->setSingleStep(1);
            value->setCompact(true);
            m_table->setCellWidget(i, ColValue, value);
            if (m_engine && m_fixture) // a show value: marked, its right-click (preset, timeline, interaction)
                attachShowValue(value, m_engine, m_undo, {QStringLiteral("fixture/%1/ch%2").arg(m_fixture).arg(r.channel)});
            connect(value, &SliderField::valueEdited, this, [this, i](double v) {
                FixtureChannel c = m_rows[size_t(i)].now;
                c.value = quint8(std::clamp(int(std::lround(v)), 0, 255));
                change(i, c);
            });
        } else {
            const bool ignored = r.now.role == ChannelRole::Ignore;
            auto *what = new QTableWidgetItem(ignored ? QStringLiteral("left to others") : QStringLiteral("pixel mapping"));
            if (ignored) what->setToolTip(kIgnoreTip);
            what->setForeground(QColor(140, 140, 150));
            m_table->setItem(i, ColValue, what);
        }

        auto *reset = new QToolButton;
        reset->setAutoRaise(true);
        if (r.type) {
            reset->setText(QStringLiteral("↺"));
            reset->setToolTip(QStringLiteral("Back to the type's: %1").arg(r.type->role == ChannelRole::Fixed
                                                                             ? QStringLiteral("Fixed %1").arg(r.type->value)
                                                                             : channelRoleName(r.type->role)));
            connect(reset, &QToolButton::clicked, this, [this, i] {
                QTimer::singleShot(0, this, [this, i] {
                    if (i < int(m_rows.size()) && m_rows[size_t(i)].type) change(i, *m_rows[size_t(i)].type);
                });
            });
        } else {
            reset->setText(QStringLiteral("×"));
            reset->setToolTip(QStringLiteral("Take this channel away"));
            connect(reset, &QToolButton::clicked, this, [this, i] {
                QTimer::singleShot(0, this, [this, i] {
                    if (i >= int(m_rows.size())) return;
                    m_rows.erase(m_rows.begin() + i);
                    rebuild();
                    emit edited();
                });
            });
        }
        m_table->setCellWidget(i, ColReset, reset);
    }
    m_updating = false;
    showValues();
}

void ChannelEditor::showValues()
{
    m_updating = true;
    for (int i = 0; i < int(m_rows.size()) && i < m_table->rowCount(); ++i) {
        const Row &r = m_rows[size_t(i)];
        if (auto *value = qobject_cast<SliderField *>(m_table->cellWidget(i, ColValue)))
            if (!value->isDragging() && std::abs(value->value() - r.now.value) > 1e-9) value->setValue(r.now.value);
        if (auto *reset = qobject_cast<QToolButton *>(m_table->cellWidget(i, ColReset)); reset && r.type)
            reset->setEnabled(!same(r.now, *r.type));
    }
    m_updating = false;
}

std::vector<ChannelEditor::Row> ChannelEditor::rowsOf(const FixtureType &t)
{
    FixtureType e = explicitChannels(t);
    std::sort(e.dmxMap.begin(), e.dmxMap.end(), [](const FixtureSlot &a, const FixtureSlot &b) { return a.offset < b.offset; });
    std::vector<Row> rows;
    for (const FixtureSlot &s : e.dmxMap) rows.push_back({s.offset + 1, s.pixel, s.channel, s.channel});
    return rows;
}

std::vector<ChannelEditor::Row> ChannelEditor::rowsOf(const FixturePatch &p, const Fixture &f)
{
    const std::optional<FixtureType> t = p.resolvedType(f);
    if (!t) return {};
    Fixture plain = f;
    plain.channels.clear();
    std::map<int, FixtureChannel> type;
    for (const Row &r : rowsOf(*p.resolvedType(plain))) type[r.channel] = r.now;
    std::vector<Row> rows = rowsOf(*t);
    for (Row &r : rows) {
        const auto it = type.find(r.channel);
        r.type = it != type.end() ? std::optional<FixtureChannel>(it->second) : std::nullopt;
    }
    return rows;
}

std::map<int, FixtureChannel> ChannelEditor::ownChannels(const std::vector<Row> &rows)
{
    std::map<int, FixtureChannel> own;
    for (const Row &r : rows)
        if (!r.type || !same(r.now, *r.type)) own[r.channel] = FixtureChannel{r.now.role, false, r.now.value};
    return own;
}

FixtureType ChannelEditor::withRows(const FixtureType &t, const std::vector<Row> &rows)
{
    FixtureType r = explicitChannels(t);
    r.dmxMap.clear();
    for (const Row &row : rows) r.dmxMap.push_back({row.channel - 1, row.now, row.pixel});
    return r;
}
