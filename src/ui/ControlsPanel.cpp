#include "Naming.h"
#include "ControlsPanel.h"
#include "AdsrCurve.h"
#include "ColorPicker.h"
#include "ControlsWeb.h"
#include "Engine.h"
#include <QSettings>
#include <QShowEvent>
#include <QStackedWidget>
#include "InteractionPanel.h"
#include "PresetEditor.h"
#include "TimelinePanel.h"
#include "Widgets.h"
#include <array>
#include <QSpinBox>
#include <functional>
#include <QPainter>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QShortcut>
#include <QStackedWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
// The whole list, before and after: the definitions of the controls are small and an edit changes one entry
class SetControlsCommand final : public QUndoCommand
{
public:
    SetControlsCommand(Engine *e, QJsonArray before, QJsonArray after, const QString &text)
        : QUndoCommand(text), m_e(e), m_before(std::move(before)), m_after(std::move(after))
    {
    }
    void undo() override { m_e->setControlsJson(m_before, nullptr, true); }
    void redo() override
    {
        if (m_first) {
            m_first = false; // the edit is already made
            return;
        }
        m_e->setControlsJson(m_after, nullptr, true);
    }

private:
    Engine *m_e;
    QJsonArray m_before, m_after;
    bool m_first = true;
};

class ControlNameLabel final : public QLabel
{
public:
    using QLabel::QLabel;
    std::function<void()> edit;

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && edit) {
            edit();
            event->accept();
        } else {
            QLabel::mouseDoubleClickEvent(event);
        }
    }
};

int decimalsFor(double step)
{
    if (step >= 1) return 0;
    return std::clamp(int(std::ceil(-std::log10(std::max(step, 1e-6)))), 0, 6);
}
} // namespace


// A row of bars to take in the hand: a Multi-slider's numbers, each a bar, dragged across (the same gesture as the remote's)
class MultiBars : public QWidget
{
public:
    std::function<void(const std::vector<double> &)> edited;
    explicit MultiBars(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(80);
        setMouseTracking(false);
    }
    void setRange(double lo, double hi) { m_lo = lo; m_hi = hi; update(); }
    void setValues(const std::vector<double> &v)
    {
        if (m_down) return;
        m_v = v;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(37, 37, 43));
        const int n = int(m_v.size());
        if (!n) return;
        const double w = double(width()) / n, span = m_hi - m_lo;
        for (int i = 0; i < n; ++i) {
            const double f = span > 0 ? std::clamp((m_v[size_t(i)] - m_lo) / span, 0.0, 1.0) : 0.0;
            const QRectF bar(i * w + 2, height() * (1 - f), w - 4, height() * f);
            p.fillRect(bar, QColor(41, 199, 224, 90));
            p.fillRect(QRectF(bar.left(), bar.top(), bar.width(), 3), QColor(41, 199, 224));
        }
    }
    void mousePressEvent(QMouseEvent *e) override { m_down = true; set(e->position()); }
    void mouseMoveEvent(QMouseEvent *e) override { if (m_down) set(e->position()); }
    void mouseReleaseEvent(QMouseEvent *) override { m_down = false; }

private:
    void set(QPointF at)
    {
        const int n = int(m_v.size());
        if (!n) return;
        const int i = std::clamp(int(at.x() / (double(width()) / n)), 0, n - 1);
        m_v[size_t(i)] = m_lo + (m_hi - m_lo) * std::clamp(1 - at.y() / height(), 0.0, 1.0);
        update();
        if (edited) edited(m_v);
    }
    std::vector<double> m_v;
    double m_lo = 0, m_hi = 1;
    bool m_down = false;
};


// A pad with several points to take in the hand: a drag moves the point nearest to where it starts
class PointsPad : public QWidget
{
public:
    std::function<void(const std::vector<double> &)> edited; // x1, y1, x2, y2…
    explicit PointsPad(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(120, 120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }
    QSize sizeHint() const override { return QSize(180, 180); }
    void setRange(double lo, double hi) { m_lo = lo; m_hi = hi; update(); }
    void setPoints(const std::vector<std::array<double, 2>> &p)
    {
        if (m_grab >= 0) return;
        m_p = p;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(37, 37, 43));
        p.setPen(QColor(60, 60, 70));
        p.drawLine(width() / 2, 0, width() / 2, height());
        p.drawLine(0, height() / 2, width(), height() / 2);
        for (size_t i = 0; i < m_p.size(); ++i) {
            const QPointF c = toWidget(m_p[i]);
            const QColor color = QColor::fromHsvF(std::fmod(0.52 + 0.17 * double(i), 1.0), 0.7, 0.95);
            p.setPen(QPen(color, 3));
            p.setBrush(QColor(242, 242, 245));
            p.drawEllipse(c, 9, 9);
            p.setPen(QColor(20, 20, 24));
            p.drawText(QRectF(c.x() - 9, c.y() - 9, 18, 18), Qt::AlignCenter, QString::number(i + 1));
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        double best = 1e18;
        m_grab = -1;
        for (size_t i = 0; i < m_p.size(); ++i) {
            const QPointF d = toWidget(m_p[i]) - e->position();
            const double q = d.x() * d.x() + d.y() * d.y();
            if (q < best) {
                best = q;
                m_grab = int(i);
            }
        }
        move(e->position());
    }
    void mouseMoveEvent(QMouseEvent *e) override { if (m_grab >= 0) move(e->position()); }
    void mouseReleaseEvent(QMouseEvent *) override { m_grab = -1; }

private:
    QPointF toWidget(const std::array<double, 2> &q) const
    {
        const double span = m_hi - m_lo;
        return QPointF(span > 0 ? (q[0] - m_lo) / span * width() : 0, span > 0 ? (1 - (q[1] - m_lo) / span) * height() : 0);
    }
    void move(QPointF at)
    {
        if (m_grab < 0 || m_grab >= int(m_p.size())) return;
        m_p[size_t(m_grab)] = {m_lo + (m_hi - m_lo) * std::clamp(at.x() / width(), 0.0, 1.0), m_lo + (m_hi - m_lo) * std::clamp(1 - at.y() / height(), 0.0, 1.0)};
        update();
        if (edited) {
            std::vector<double> flat;
            for (const auto &q : m_p) flat.push_back(q[0]), flat.push_back(q[1]);
            edited(flat);
        }
    }
    std::vector<std::array<double, 2>> m_p;
    double m_lo = 0, m_hi = 1;
    int m_grab = -1;
};

ControlsPanel::ControlsPanel(Engine *engine, QUndoStack *undo, QWidget *parent) : QWidget(parent), m_engine(engine), m_undo(undo)
{
    setObjectName(QStringLiteral("controlsPanel"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(8, 8, 4, 4);
    v->setSpacing(6);
    auto *head = new QHBoxLayout;
    head->addWidget(new QLabel(QStringLiteral("<b>Controls</b>")));
    head->addStretch();
    auto *add = new QToolButton;
    add->setObjectName(QStringLiteral("controlsAdd"));
    add->setText(QStringLiteral("+ New"));
    add->setToolTip(QStringLiteral("Make a slider, a switch, a selector, a button or a color of your own"));
    add->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(add);
    for (int k = 0; k < ShowControl::kKindCount; ++k)
        menu->addAction(ShowControl::kindName(ShowControl::Kind(k)), this, [this, k] { addControl(k); })->setObjectName(QStringLiteral("controlsAdd%1").arg(k));
    add->setMenu(menu);
    head->addWidget(add);
    // With a browser, the panel is the remote's page (the same grid, Edit mode and widgets a phone has); the list of rows stays as the
    // other way to see them, and the page can be opened in a window to lay it out on a large area
    m_pageButton = new QToolButton;
    m_pageButton->setObjectName(QStringLiteral("controlsPageButton"));
    m_pageButton->setText(QStringLiteral("Page"));
    m_pageButton->setCheckable(true);
    m_pageButton->setToolTip(QStringLiteral("The page of the remote, as a phone shows it: a free grid (Edit to lay it out), pages for the groups, big faders, "
                                            "encoders, pads. Off: the list of rows"));
    m_windowButton = new QToolButton;
    m_windowButton->setObjectName(QStringLiteral("controlsWindowButton"));
    m_windowButton->setText(QStringLiteral("⤢"));
    m_windowButton->setToolTip(QStringLiteral("The page in a window of its own"));
    head->addWidget(m_pageButton);
    head->addWidget(m_windowButton);
    m_pageButton->setVisible(controlsweb::available());
    m_windowButton->setVisible(controlsweb::available());
    v->addLayout(head);
    m_stack = new QStackedWidget;
    auto *classic = new QWidget;
    auto *classicColumn = new QVBoxLayout(classic);
    classicColumn->setContentsMargins(0, 0, 0, 0);
    classicColumn->setSpacing(6);
    auto *note = new QLabel(QStringLiteral("Each control is a value of the show: an interaction can follow it or drive it, a timeline can animate it, "
                                           "OSC and the remote can move it."));
    note->setWordWrap(true);
    note->setStyleSheet("color:#888; font-size:11px;");
    classicColumn->addWidget(note);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget;
    m_list = new QVBoxLayout(inner);
    m_list->setContentsMargins(0, 0, 0, 0);
    m_list->setSpacing(4);
    scroll->setWidget(inner);
    classicColumn->addWidget(scroll, 1);
    m_stack->addWidget(classic); // 0: the rows; 1: the page (made when it is first shown)
    v->addWidget(m_stack, 1);
    connect(m_pageButton, &QToolButton::toggled, this, [this](bool on) { showPage(on); });
    connect(m_windowButton, &QToolButton::clicked, this, &ControlsPanel::openWindow);

    m_empty = new QLabel(QStringLiteral("No control yet. Use + New."));
    m_empty->setStyleSheet("color:#888;");
    connect(m_engine, &Engine::controlsChanged, this, &ControlsPanel::rebuild);
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (isVisible()) refreshValues();
    });
    m_timer->start();
    rebuild();
}

void ControlsPanel::editList(const QString &text, const std::function<void(QJsonArray &)> &change)
{
    const QJsonArray before = m_engine->controlsJson();
    QJsonArray after = before;
    change(after);
    QString error;
    if (!m_engine->setControlsJson(after, &error)) return;
    m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), text));
    emit edited();
}

void ControlsPanel::addControl(int kind)
{
    ShowControl c;
    c.kind = ShowControl::Kind(kind);
    c.name = ShowControl::kindName(c.kind);
    if (c.kind == ShowControl::Kind::Choice) c.choices = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    if (c.kind == ShowControl::Kind::Pad) c.def = c.value = c.value2 = 0.5; // the middle of its square
    if (c.kind == ShowControl::Kind::Adsr) c.max = 2; // the longest of its times, in seconds
    if (c.kind == ShowControl::Kind::Multi) c.multi.assign(size_t(c.count), c.def);
    if (c.kind == ShowControl::Kind::Points) { // three points in a row across the middle of the square
        c.count = 3;
        c.def = 0.5;
        c.points = {{0.25, 0.5}, {0.5, 0.5}, {0.75, 0.5}};
    }
    const QJsonArray before = m_engine->controlsJson();
    if (!m_engine->addControl(c)) return;
    m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), QStringLiteral("Add Control")));
    emit edited();
}

void ControlsPanel::editControl(quint64 id)
{
    const auto current = m_engine->control(id);
    if (!current) return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("%1 — %2").arg(ShowControl::kindName(current->kind), current->name));
    auto *form = new QFormLayout(&dialog);
    auto *name = new QLineEdit(current->name);
    form->addRow(QStringLiteral("Name"), name);
    if (current->kind == ShowControl::Kind::Points)
        form->addRow(QStringLiteral("Address"), new QLabel(QStringLiteral("/control/%1 (x1, y1, x2, y2…) · …/p1/x · …/p1/y · …/p2/x …").arg(current->key)));
    else if (current->kind == ShowControl::Kind::Multi)
        form->addRow(QStringLiteral("Address"), new QLabel(QStringLiteral("/control/%1 (one float per slider) · …/1 · …/2 …").arg(current->key)));
    else if (current->kind == ShowControl::Kind::Adsr)
        form->addRow(QStringLiteral("Address"), new QLabel(QStringLiteral("/control/%1 (attack, decay, sustain, release) · …/attack · …/decay · …/sustain · …/release").arg(current->key)));
    else if (current->kind == ShowControl::Kind::Pad)
        form->addRow(QStringLiteral("Address"), new QLabel(QStringLiteral("/control/%1/x · /control/%1/y").arg(current->key)));
    else if (current->kind != ShowControl::Kind::Group)
        form->addRow(QStringLiteral("Address"), new QLabel(QStringLiteral("/control/%1").arg(current->key)));
    QDoubleSpinBox *min = nullptr, *max = nullptr, *step = nullptr, *def = nullptr;
    QLineEdit *unit = nullptr;
    QPlainTextEdit *choices = nullptr;
    QDoubleSpinBox *longest = nullptr;
    QSpinBox *count = nullptr;
    if (current->kind == ShowControl::Kind::Multi || current->kind == ShowControl::Kind::Points) {
        const bool pts = current->kind == ShowControl::Kind::Points;
        count = new QSpinBox;
        count->setRange(pts ? 1 : 2, pts ? ShowControl::kPointsMax : ShowControl::kMultiMax);
        count->setValue(current->count);
        form->addRow(pts ? QStringLiteral("Points") : QStringLiteral("Sliders"), count);
    }
    if (current->kind == ShowControl::Kind::Slider || current->kind == ShowControl::Kind::Pad || current->kind == ShowControl::Kind::Multi ||
        current->kind == ShowControl::Kind::Points) {
        min = new QDoubleSpinBox;
        max = new QDoubleSpinBox;
        step = new QDoubleSpinBox;
        def = new QDoubleSpinBox;
        for (auto *b : {min, max, def}) {
            b->setRange(-1e9, 1e9);
            b->setDecimals(4);
        }
        step->setRange(1e-6, 1e9);
        step->setDecimals(6);
        min->setValue(current->min);
        max->setValue(current->max);
        step->setValue(current->step);
        def->setValue(current->def);
        unit = new QLineEdit(current->unit);
        unit->setPlaceholderText(QStringLiteral("e.g. %, Hz, s"));
        unit->setEnabled(current->kind == ShowControl::Kind::Slider);
        form->addRow(QStringLiteral("Minimum"), min);
        form->addRow(QStringLiteral("Maximum"), max);
        form->addRow(QStringLiteral("Step"), step);
        form->addRow(QStringLiteral("Default"), def);
        form->addRow(QStringLiteral("Unit"), unit);
    } else if (current->kind == ShowControl::Kind::Choice) {
        choices = new QPlainTextEdit(current->choices.join(QLatin1Char('\n')));
        choices->setMinimumHeight(90);
        form->addRow(QStringLiteral("Choices\n(one per line)"), choices);
    } else if (current->kind == ShowControl::Kind::Adsr) {
        longest = new QDoubleSpinBox;
        longest->setRange(0.1, 60);
        longest->setDecimals(2);
        longest->setSuffix(QStringLiteral(" s"));
        longest->setValue(current->max);
        longest->setToolTip(QStringLiteral("The longest attack, decay or release its curve can take"));
        form->addRow(QStringLiteral("Longest time"), longest);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    ShowControl change = *current;
    change.name = name->text().trimmed().isEmpty() ? current->name : name->text().trimmed();
    if (min) {
        change.min = min->value();
        change.max = max->value();
        change.step = step->value();
        change.def = def->value();
        change.unit = unit->text();
    }
    if (longest) change.max = longest->value();
    if (count) change.count = count->value();
    if (choices) {
        QStringList list;
        for (const QString &line : choices->toPlainText().split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty() && !list.contains(line.trimmed())) list << line.trimmed();
        if (!list.isEmpty()) change.choices = list;
    }
    const QJsonArray before = m_engine->controlsJson();
    if (!m_engine->updateControl(id, change)) return;
    m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), QStringLiteral("Edit Control")));
    emit edited();
}

void ControlsPanel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_pageTried || !controlsweb::available()) return;
    m_pageTried = true;
    if (QSettings().value(QStringLiteral("controls/page"), true).toBool()) m_pageButton->setChecked(true);
}

void ControlsPanel::showPage(bool page)
{
    QSettings().setValue(QStringLiteral("controls/page"), page);
    if (page && !m_web) {
        m_web = controlsweb::make(controlsweb::url()); // null: no browser, or its server is not up
        if (m_web) m_stack->addWidget(m_web);
    }
    if (page && !m_web) { // nothing to show it with: the rows
        const QSignalBlocker block(m_pageButton);
        m_pageButton->setChecked(false);
        page = false;
    }
    m_stack->setCurrentIndex(page ? 1 : 0);
}

void ControlsPanel::openWindow()
{
    QWidget *view = controlsweb::make(controlsweb::url());
    if (!view) return;
    auto *window = new QWidget(this, Qt::Window);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(QStringLiteral("Controls"));
    auto *column = new QVBoxLayout(window);
    column->setContentsMargins(0, 0, 0, 0);
    column->addWidget(view);
    window->resize(1100, 760);
    window->show();
}

void ControlsPanel::rebuild()
{
    m_filling = true;
    while (QLayoutItem *item = m_list->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            if (w == m_empty) w->hide();
            else w->deleteLater();
        }
        delete item;
    }
    m_rows.clear();
    const std::vector<ShowControl> controls = m_engine->controls();
    if (controls.empty()) {
        m_list->addWidget(m_empty);
        m_empty->show();
    }
    fillContainer(m_list, nullptr, 0, 1, controls);
    m_list->addStretch(1);
    m_filling = false;
}

// What a group (0: the page) holds, in the list's order: a control is a row, a group a frame. `grid`: where a group lays them
// out in columns; none: the page, one under the other
void ControlsPanel::fillContainer(QVBoxLayout *column, QGridLayout *grid, quint64 group, int columns, const std::vector<ShowControl> &all)
{
    std::vector<const ShowControl *> members;
    for (const ShowControl &c : all)
        if (c.group == group) members.push_back(&c);
    int col = 0, row = 0;
    for (size_t k = 0; k < members.size(); ++k) {
        const ShowControl &c = *members[k];
        const bool frame = c.kind == ShowControl::Kind::Group;
        QWidget *w = frame ? makeGroup(c, members, k, all) : makeRow(c, members, k);
        if (!grid) {
            column->addWidget(w);
            continue;
        }
        const int span = frame ? columns : std::min(std::max(c.span, 1), columns);
        if (col + span > columns) {
            col = 0;
            ++row;
        }
        grid->addWidget(w, row, col, 1, span);
        col += span;
        if (col >= columns) {
            col = 0;
            ++row;
        }
    }
}

// A group: its name and gear, then its controls in its columns
QWidget *ControlsPanel::makeGroup(const ShowControl &c, const std::vector<const ShowControl *> &siblings, size_t position,
                                  const std::vector<ShowControl> &all)
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("controlGroup"));
    frame->setFrameShape(QFrame::StyledPanel);
    auto *v = new QVBoxLayout(frame);
    v->setContentsMargins(8, 4, 8, 8);
    v->setSpacing(6);
    auto *head = new QHBoxLayout;
    head->addWidget(nameField(c), 1);
    head->addWidget(gearButton(c, siblings, position, all));
    v->addLayout(head);
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    for (int col = 0; col < c.columns; ++col) grid->setColumnStretch(col, 1);
    v->addLayout(grid);
    fillContainer(v, grid, c.id, c.columns, all);
    return frame;
}

// Its name, in place (double-click)
QWidget *ControlsPanel::nameField(const ShowControl &c)
{
    const quint64 id = c.id;
    const bool group = c.kind == ShowControl::Kind::Group;
    auto *name = new QStackedWidget;
    name->setMinimumWidth(84);
    if (!group) name->setMaximumWidth(140);
    auto *label = new ControlNameLabel(c.name);
    label->setTextFormat(Qt::PlainText);
    label->setObjectName(QStringLiteral("controlName"));
    if (group) label->setStyleSheet(QStringLiteral("font-weight:bold;"));
    label->setToolTip(group ? QStringLiteral("Double-click to rename") : QStringLiteral("Double-click to rename · /control/%1").arg(c.key));
    auto *editor = new QLineEdit(c.name);
    editor->setObjectName(QStringLiteral("controlNameEditor"));
    name->addWidget(label);
    name->addWidget(editor);
    label->edit = [name, label, editor] {
        editor->setText(label->text());
        name->setCurrentWidget(editor);
        editor->setFocus();
        editor->selectAll();
    };
    auto *cancel = new QShortcut(QKeySequence(Qt::Key_Escape), editor);
    cancel->setContext(Qt::WidgetShortcut);
    connect(cancel, &QShortcut::activated, name, [name, label] { name->setCurrentWidget(label); });
    connect(editor, &QLineEdit::editingFinished, name, [this, id, name, label, editor] {
        if (name->currentWidget() != editor) return;
        const QString text = editor->text().trimmed();
        name->setCurrentWidget(label);
        auto control = m_engine->control(id);
        if (!control || text.isEmpty() || control->name == text) return;
        control->name = text;
        applyControl(id, *control, QStringLiteral("Rename Control"));
    });
    return name;
}

// The control `id` replaced by `change`, as one undo step
void ControlsPanel::applyControl(quint64 id, const ShowControl &change, const QString &text)
{
    const QJsonArray before = m_engine->controlsJson();
    if (!m_engine->updateControl(id, change)) return;
    m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), text));
    emit edited();
}

// Its gear: edit, duplicate, where it is (up, down, its width, its group), delete
QToolButton *ControlsPanel::gearButton(const ShowControl &c, const std::vector<const ShowControl *> &siblings, size_t position,
                                       const std::vector<ShowControl> &all)
{
    const quint64 id = c.id;
    const bool group = c.kind == ShowControl::Kind::Group;
    auto *gear = new QToolButton;
    gear->setText(QStringLiteral("⋯"));
    gear->setToolTip(QStringLiteral("Edit, move, duplicate, delete"));
    gear->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(gear);
    menu->addAction(QStringLiteral("Edit…"), this, [this, id] { editControl(id); });
    menu->addAction(QStringLiteral("Duplicate"), this, [this, id] {
        const auto c = m_engine->control(id);
        if (!c) return;
        ShowControl copy = *c;
        QStringList taken;
        for (const ShowControl &other : m_engine->controls()) taken << other.name;
        copy.name = numberedName(copy.name, taken);
        const QJsonArray before = m_engine->controlsJson();
        if (!m_engine->addControl(copy)) return;
        m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), QStringLiteral("Duplicate Control")));
        emit edited();
    });
    menu->addSeparator();
    // Up and down among what is beside it (the same group)
    std::vector<quint64> order;
    for (const ShowControl *s : siblings) order.push_back(s->id);
    auto mover = [this, id, order, position, all](int delta) {
        const size_t to = size_t(int(position) + delta);
        if (to >= order.size()) return;
        int at = -1;
        for (int k = 0; k < int(all.size()); ++k)
            if (all[size_t(k)].id == order[to]) at = k;
        if (at < 0) return;
        const QJsonArray before = m_engine->controlsJson();
        m_engine->moveControl(id, at);
        m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), QStringLiteral("Move Control")));
        emit edited();
    };
    menu->addAction(QStringLiteral("Move up"), this, [mover] { mover(-1); })->setEnabled(position > 0);
    menu->addAction(QStringLiteral("Move down"), this, [mover] { mover(1); })->setEnabled(position + 1 < siblings.size());
    // Its width in its group's columns; a group's own columns
    const auto numbers = [this, id](QMenu *into, const QString &title, int current, std::function<void(ShowControl &, int)> set) {
        auto *m = into->addMenu(title);
        for (int n = 1; n <= 4; ++n) {
            auto *a = m->addAction(QString::number(n), this, [this, id, n, set] {
                if (auto control = m_engine->control(id)) {
                    set(*control, n);
                    applyControl(id, *control, QStringLiteral("Lay Out Control"));
                }
            });
            a->setCheckable(true);
            a->setChecked(n == current);
        }
    };
    if (group) numbers(menu, QStringLiteral("Columns"), c.columns, [](ShowControl &x, int n) { x.columns = n; });
    else if (c.group) numbers(menu, QStringLiteral("Width (columns)"), c.span, [](ShowControl &x, int n) { x.span = n; });
    // Into a group, or out of one
    auto *into = menu->addMenu(QStringLiteral("Move to Group"));
    const auto moveTo = [this, id](quint64 target) {
        if (auto control = m_engine->control(id)) {
            control->group = target;
            applyControl(id, *control, QStringLiteral("Move Control to Group"));
        }
    };
    into->addAction(QStringLiteral("The Page"), this, [moveTo] { moveTo(0); })->setEnabled(c.group != 0);
    bool any = false;
    for (const ShowControl &g : all) {
        if (g.kind != ShowControl::Kind::Group || g.id == id) continue;
        // Not inside itself: a group does not go into one of its own
        bool inside = false;
        for (quint64 up = g.group; up && group; ) {
            if (up == id) { inside = true; break; }
            const auto parent = std::find_if(all.begin(), all.end(), [up](const ShowControl &x) { return x.id == up; });
            up = parent == all.end() ? 0 : parent->group;
        }
        if (inside) continue;
        any = true;
        into->addAction(g.name, this, [moveTo, gid = g.id] { moveTo(gid); })->setEnabled(c.group != g.id);
    }
    if (!any) into->addAction(QStringLiteral("(no group yet: + New › Group)"))->setEnabled(false);
    menu->addSeparator();
    menu->addAction(group ? QStringLiteral("Delete Group (keeps its controls)") : QStringLiteral("Delete"), this, [this, id] {
        const QJsonArray before = m_engine->controlsJson();
        if (!m_engine->removeControl(id)) return;
        m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), QStringLiteral("Delete Control")));
        emit edited();
    });
    gear->setMenu(menu);
    return gear;
}

QWidget *ControlsPanel::makeRow(const ShowControl &c, const std::vector<const ShowControl *> &siblings, size_t position)
{
    const quint64 id = c.id;
    auto *row = new QWidget;
    row->setObjectName(QStringLiteral("controlRow"));
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    h->addWidget(nameField(c));
    QWidget *field = nullptr;
    switch (c.kind) {
    case ShowControl::Kind::Group: break; // a frame: makeGroup
    case ShowControl::Kind::Points: {
        auto *pad = new PointsPad;
        pad->setObjectName(QStringLiteral("control_%1").arg(c.key));
        pad->setRange(c.min, c.max);
        pad->setPoints(c.points);
        pad->edited = [this, id](const std::vector<double> &v) { m_engine->setControlPoints(id, v); };
        field = pad;
        h->addWidget(pad, 1);
        break;
    }
    case ShowControl::Kind::Multi: {
        auto *bars = new MultiBars;
        bars->setObjectName(QStringLiteral("control_%1").arg(c.key));
        bars->setRange(c.min, c.max);
        bars->setValues(c.multi);
        bars->edited = [this, id](const std::vector<double> &v) { m_engine->setControlMulti(id, v); };
        field = bars;
        h->addWidget(bars, 1);
        break;
    }
    case ShowControl::Kind::Adsr: {
        // An envelope to take in the hand (the instruments' and the interactions' own widget): its four numbers are parameters
        auto *curve = new AdsrCurve;
        curve->setObjectName(QStringLiteral("control_%1").arg(c.key));
        curve->shape = AdsrCurve::Shape::Straight;
        curve->longest = c.max;
        curve->setMinimumHeight(90);
        curve->setValues({c.adsr[0], c.adsr[1], c.adsr[2], c.adsr[3]});
        curve->edited = [this, id](const AdsrCurve::Values &v) { m_engine->setControlAdsr(id, {v.attack, v.decay, v.sustain, v.release}); };
        field = curve;
        h->addWidget(curve, 1);
        break;
    }
    case ShowControl::Kind::Pad: {
        auto *pad = new XyPad;
        pad->setObjectName(QStringLiteral("control_%1").arg(c.key));
        pad->setRange(c.min, c.max);
        pad->setDefault(c.def);
        pad->setDecimals(decimalsFor(c.step));
        pad->setValue(c.value, c.value2);
        connect(pad, &XyPad::valueEdited, this, [this, id](double x, double y) { m_engine->setControlPoint(id, x, y); });
        field = pad;
        h->addWidget(pad, 1);
        break;
    }
    case ShowControl::Kind::Slider: {
        auto *s = new SliderField;
        s->setObjectName(QStringLiteral("control_%1").arg(c.key));
        s->setRange(c.min, c.max);
        s->setDecimals(decimalsFor(c.step));
        s->setSingleStep(c.step);
        if (!c.unit.isEmpty()) s->setSuffix(c.unit);
        s->setValue(c.value);
        connect(s, &SliderField::valueEdited, this, [this, id](double v) { m_engine->setControlValue(id, v); });
        field = s;
        h->addWidget(s, 1);
        break;
    }
    case ShowControl::Kind::Toggle: {
        auto *f = new FlagBox(QString());
        f->setObjectName(QStringLiteral("control_%1").arg(c.key));
        f->setChecked(c.value >= 0.5);
        connect(f, &QCheckBox::toggled, this, [this, id](bool on) {
            if (!m_filling) m_engine->setControlValue(id, on ? 1.0 : 0.0);
        });
        field = f;
        h->addWidget(f);
        h->addStretch(1);
        break;
    }
    case ShowControl::Kind::Choice: {
        auto *b = new QComboBox;
        b->setObjectName(QStringLiteral("control_%1").arg(c.key));
        b->addItems(c.choices);
        b->setCurrentIndex(int(c.value));
        connect(b, qOverload<int>(&QComboBox::activated), this, [this, id](int k) { m_engine->setControlValue(id, k); });
        field = b;
        h->addWidget(b, 1);
        break;
    }
    case ShowControl::Kind::Button: {
        auto *b = new QPushButton(QStringLiteral("Hold"));
        b->setObjectName(QStringLiteral("control_%1").arg(c.key));
        b->setToolTip(QStringLiteral("1 while it is held down, 0 when let go: a gate for an Envelope, a flash, a strobe"));
        connect(b, &QPushButton::pressed, this, [this, id] { m_engine->setControlValue(id, 1.0); });
        connect(b, &QPushButton::released, this, [this, id] { m_engine->setControlValue(id, 0.0); });
        field = b;
        h->addWidget(b, 1);
        break;
    }
    case ShowControl::Kind::Color: {
        auto *b = new QPushButton;
        b->setObjectName(QStringLiteral("control_%1").arg(c.key));
        b->setMinimumHeight(24);
        const QColor color = QColor::fromRgbF(float(c.rgb[0]), float(c.rgb[1]), float(c.rgb[2]));
        b->setStyleSheet(QStringLiteral("background:%1; border:1px solid #555;").arg(color.name()));
        connect(b, &QPushButton::clicked, this, [this, id, b] {
            const auto now = m_engine->control(id);
            if (!now) return;
            const QColor start = QColor::fromRgbF(float(now->rgb[0]), float(now->rgb[1]), float(now->rgb[2]));
            const QColor picked = ColorPicker::getColor(start, this, QStringLiteral("Control color"), QStringLiteral("controls/color"), false);
            if (!picked.isValid()) return;
            m_engine->setControlColor(id, picked.redF(), picked.greenF(), picked.blueF());
            b->setStyleSheet(QStringLiteral("background:%1; border:1px solid #555;").arg(picked.name()));
        });
        field = b;
        h->addWidget(b, 1);
        break;
    }
    }
    const QString path = QStringLiteral("control/") + c.key;
    if (field) {
        // Its value as any value: in a preset (Edit mode, a click on its name, right-click), on a timeline, learned by an interaction
        QStringList multiPaths;
        for (int i = 0; i < c.count && c.kind == ShowControl::Kind::Multi; ++i) multiPaths << path + QLatin1Char('/') + QString::number(i + 1);
        QStringList pointPaths;
        for (int i = 0; i < c.count && c.kind == ShowControl::Kind::Points; ++i)
            pointPaths << path + QStringLiteral("/p%1/x").arg(i + 1) << path + QStringLiteral("/p%1/y").arg(i + 1);
        const QStringList paths = c.kind == ShowControl::Kind::Points ? pointPaths
                                  : c.kind == ShowControl::Kind::Multi ? multiPaths
                                  : c.kind == ShowControl::Kind::Adsr
                                      ? QStringList{path + QStringLiteral("/attack"), path + QStringLiteral("/decay"), path + QStringLiteral("/sustain"), path + QStringLiteral("/release")}
                                  : c.kind == ShowControl::Kind::Pad ? QStringList{path + QStringLiteral("/x"), path + QStringLiteral("/y")}
                                  : c.kind == ShowControl::Kind::Color
                                      ? QStringList{path + QStringLiteral("/r"), path + QStringLiteral("/g"), path + QStringLiteral("/b")}
                                      : QStringList{path};
        if (PresetEditor *pe = PresetEditor::instance()) {
            pe->track(field, 0, paths);
            if (auto *label = row->findChild<QLabel *>(QStringLiteral("controlName"))) pe->track(label, 0, paths);
        }
        field->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(field, &QWidget::customContextMenuRequested, this, [this, field, paths](const QPoint &pos) {
            auto *menu = new QMenu(field);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            if (PresetEditor *pe = PresetEditor::instance()) pe->fillMenu(menu, 0, paths);
            TimelineWindow::fillParameterMenu(menu, m_engine, m_undo, 0, paths, field);
            InteractionPanel::fillValueMenu(menu, m_engine, 0, paths);
            if (menu->isEmpty()) {
                delete menu;
                return;
            }
            menu->popup(field->mapToGlobal(pos));
        });
    }
    h->addWidget(gearButton(c, siblings, position, m_engine->controls()));
    m_rows.push_back({id, field});
    return row;
}

void ControlsPanel::refreshValues()
{
    const std::vector<ShowControl> controls = m_engine->controls();
    m_filling = true;
    for (const Row &row : m_rows) {
        if (!row.field || row.field->hasFocus()) continue;
        const auto it = std::find_if(controls.begin(), controls.end(), [&](const ShowControl &c) { return c.id == row.id; });
        if (it == controls.end()) continue;
        const ShowControl &c = *it;
        if (auto *pts = dynamic_cast<PointsPad *>(row.field.data())) {
            pts->setPoints(c.points);
        } else if (auto *bars = dynamic_cast<MultiBars *>(row.field.data())) {
            bars->setValues(c.multi);
        } else if (auto *curve = dynamic_cast<AdsrCurve *>(row.field.data())) {
            curve->setValues({c.adsr[0], c.adsr[1], c.adsr[2], c.adsr[3]});
        } else if (auto *pad = qobject_cast<XyPad *>(row.field.data())) {
            if (!pad->isDragging()) pad->setValue(c.value, c.value2);
        } else if (auto *s = qobject_cast<SliderField *>(row.field.data())) {
            if (!s->isDragging()) {
                const QSignalBlocker b(s);
                s->setValue(c.value);
            }
        } else if (auto *f = qobject_cast<QCheckBox *>(row.field.data())) {
            const QSignalBlocker b(f);
            f->setChecked(c.value >= 0.5);
        } else if (auto *box = qobject_cast<QComboBox *>(row.field.data())) {
            if (!box->view()->isVisible() && box->currentIndex() != int(c.value)) {
                const QSignalBlocker b(box);
                box->setCurrentIndex(int(c.value));
            }
        } else if (auto *button = qobject_cast<QPushButton *>(row.field.data())) {
            if (c.kind == ShowControl::Kind::Color)
                button->setStyleSheet(QStringLiteral("background:%1; border:1px solid #555;")
                                          .arg(QColor::fromRgbF(float(c.rgb[0]), float(c.rgb[1]), float(c.rgb[2])).name()));
            else if (c.kind == ShowControl::Kind::Button && !button->isDown())
                button->setText(c.value >= 0.5 ? QStringLiteral("● on") : QStringLiteral("Hold"));
        }
    }
    m_filling = false;
}
