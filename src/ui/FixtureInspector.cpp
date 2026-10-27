#include "FixtureInspector.h"
#include "Commands.h"
#include "Engine.h"
#include "FixtureOverlay.h"
#include "Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>
#include <memory>
#include <vector>

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
    m_universe->setMinimum(out && out->protocol == DmxOutput::Protocol::Sacn ? 1 : 0);
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
    m_updating = false;
}
