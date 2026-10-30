// An interaction's filters in its settings, as a layer's effects: added from "+ Filter", each on or off, moved,
// removed, a Math filter's operation chosen, a Curve's editor shown — each one undo step. No Invert any more.
#include "Engine.h"
#include "AnimEditor.h"
#include "InteractionPanel.h"
#include "PresetEditor.h"
#include "Widgets.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
#include <QPointer>
#include <QMouseEvent>
#include <QProgressBar>
#include <QTreeWidget>
#include <QLabel>
#include <QToolButton>
#include <QUndoStack>
#include <cstdio>

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    theme::applyToApplication(app);
    Engine engine;
    QUndoStack undo;
    int failures = 0;
    auto check = [&](bool ok, const char *why) { if (!ok) { ++failures; std::printf("FAIL interaction_filters_ui: %s\n", why); } };
    PresetEditor presets(&engine, &undo); // as in the application: it watches the values shown
    InteractionPanel panel(&engine, &undo);
    panel.resize(720, 1400);
    panel.show();
    panel.learnFor(0, "opacity", InteractionSource::Osc);
    auto settle = [&] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        app.processEvents();
    };
    settle();
    auto filters = [&] { return engine.interactions().back().filters; };
    auto add = [&](const char *kind) {
        auto *button = panel.findChild<QToolButton *>("addFilter");
        if (!button || !button->menu()) return false;
        for (QAction *a : button->menu()->actions())
            if (a->text() == QLatin1String(kind)) {
                a->trigger();
                settle();
                return true;
            }
        return false;
    };

    bool invert = false;
    for (auto *box : panel.findChildren<QCheckBox *>()) invert = invert || box->text() == "Invert";
    check(!invert, "no Invert: a filter does it");
    check(filters().empty() && panel.findChild<QToolButton *>("addFilter"), "none at first, + Filter offered");

    check(add("Math") && add("Clamp"), "add Math, then Clamp");
    check(filters().size() == 2 && filters()[0].kind == InteractionFilter::Math && filters()[1].kind == InteractionFilter::Clamp,
          "in the order they were added");
    if (auto *op = panel.findChild<QComboBox *>("filterOp0")) {
        op->setCurrentIndex(int(MathOp::OneMinus));
        emit op->activated(int(MathOp::OneMinus));
        settle();
    }
    check(filters()[0].op == MathOp::OneMinus, "Math › 1 − x");
    check(!panel.findChild<QWidget *>("filter0a"), "1 − x has no value to set");
    auto select = [&](int k) { // a click on a filter (its frame, not a field)
        if (auto *block = panel.findChild<QWidget *>(QStringLiteral("filterBlock%1").arg(k))) {
            const QPointF at(block->width() - 4, 4);
            QMouseEvent press(QEvent::MouseButtonPress, at, block->mapToGlobal(at.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(block, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, at, block->mapToGlobal(at.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(block, &release);
        }
        settle();
    };
    select(1);
    if (auto *up = panel.findChild<QToolButton *>("filterUp")) up->click();
    settle();
    check(filters()[0].kind == InteractionFilter::Clamp && filters()[1].kind == InteractionFilter::Math, "Clamp moved before Math");
    undo.undo();
    settle();
    check(filters()[0].kind == InteractionFilter::Math, "the move undoes");
    if (auto *on = panel.findChild<QCheckBox *>("filterOn0")) on->setChecked(false);
    settle();
    check(!filters()[0].enabled, "a filter switched off");
    select(1);
    if (auto *remove = panel.findChild<QToolButton *>("removeFilter")) remove->click();
    settle();
    check(filters().size() == 1 && filters()[0].kind == InteractionFilter::Math, "Clamp removed");
    undo.undo();
    settle();
    check(filters().size() == 2, "the removal undoes");
    check(panel.findChild<QWidget *>("filtersFrame") != nullptr, "the filters in their frame");

    // Any value: a filter's setting (a number field, no slider and no bounds)
    if (auto *clampMax = panel.findChild<QDoubleSpinBox *>("filter1b")) clampMax->setValue(250);
    settle();
    check(filters()[1].kind == InteractionFilter::Clamp && filters()[1].b == 250, "a setting beyond its usual range");
    bool ranges = false, toggle = false; // a range is a Remap, a toggle a Toggle: no fields of their own
    for (auto *l : panel.findChildren<QLabel *>()) ranges = ranges || (l->isVisible() && (l->text() == "Source range" || l->text() == "Target range"));
    for (auto *box : panel.findChildren<QCheckBox *>()) toggle = toggle || (box->isVisible() && box->text() == "Toggle");
    check(!ranges && !toggle, "no ranges nor toggle of their own: filters");


    check(add("Curve") && filters().size() == 3 && panel.findChild<CurveLane *>("filterCurve2"), "a Curve, with its editor");
    check(panel.findChild<QProgressBar *>("filterMeter0") && panel.findChild<QProgressBar *>("filterMeter2") &&
              panel.findChild<QLabel *>("filterValue1"),
          "each filter shows what comes out of it");
    if (auto *list = panel.findChild<QTreeWidget *>()) check(list->columnCount() == 3, "no Value column in the list");
    // A Remap from +: from what the source gives (OSC: 0..1) to what the target takes (the composition's opacity: 0..1)
    check(add("Remap") && filters().size() == 4 && filters()[3].kind == InteractionFilter::Remap && filters()[3].a == 0 &&
              filters()[3].b == 1 && filters()[3].c == 0 && filters()[3].d == 1,
          "a Remap starts from the source's range to the target's");
    check(panel.findChild<QDoubleSpinBox *>("filter3d") != nullptr, "its four ends as numbers");
    {
        auto list = filters();
        list.pop_back();
        Interaction g = engine.interactions().back();
        g.filters = list;
        engine.setInteraction(g);
        settle();
    }
    // Drawn on: a key where it is clicked, then moved by a drag
    auto mouse = [&](QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButtons buttons) {
        QMouseEvent ev(type, pos, w->mapToGlobal(pos.toPoint()), Qt::LeftButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(w, &ev);
    };
    if (auto *lane = panel.findChild<CurveLane *>("filterCurve2")) {
        const QPointF mid(lane->width() * 0.5, lane->height() * 0.2);
        mouse(lane, QEvent::MouseButtonPress, mid, Qt::LeftButton);
        mouse(lane, QEvent::MouseButtonRelease, mid, Qt::NoButton);
        settle();
        check(filters()[2].curve.keys.size() == 3, "a click on the curve adds a key");
        lane = panel.findChild<CurveLane *>("filterCurve2");
        const auto before = filters()[2].curve.keys;
        if (lane && before.size() == 3) {
            const QPointF from(lane->width() * 0.5, lane->height() * 0.2), to(lane->width() * 0.5, lane->height() * 0.7);
            mouse(lane, QEvent::MouseButtonPress, from, Qt::LeftButton);
            for (int k = 1; k <= 5; ++k) mouse(lane, QEvent::MouseMove, from + (to - from) * (k / 5.0), Qt::LeftButton);
            mouse(lane, QEvent::MouseButtonRelease, to, Qt::NoButton);
            settle();
            const auto after = filters()[2].curve.keys;
            check(after.size() == 3 && after[1].v < before[1].v - 0.2, "a key dragged down");
        }
    }

    // A curve with keys a thousandth from its ends (as drawn in a show to cut at the end of a video): its top right
    // key dragged. Each move is an edit, the list is made again: the curve dragged stays the one under the mouse
    {
        Interaction g = engine.interactions().back();
        InteractionStage c = InteractionStage::made(InteractionFilter::Curve);
        c.curve.keys = {AnimKey{0, 0, 0}, AnimKey{0.001, 0.995, 3}, AnimKey{0.999, 1.005, 3}, AnimKey{1, 0, 3}};
        g.filters = {c};
        engine.setInteraction(g);
        settle();
        auto *lane = panel.findChild<CurveLane *>("filterCurve0");
        check(lane != nullptr, "its curve shown");
        if (lane) {
            // Where a press takes the key at 0.999 (1.005), at the top right (a press beside it would draw a key)
            QPointF at(-1, -1);
            for (int y = 2; y < 50 && at.x() < 0; y += 2)
                for (int x = lane->width() - 2; x > lane->width() - 60 && at.x() < 0; x -= 2) {
                    const size_t n = filters()[0].curve.keys.size();
                    mouse(lane, QEvent::MouseButtonPress, QPointF(x, y), Qt::LeftButton);
                    mouse(lane, QEvent::MouseButtonRelease, QPointF(x, y), Qt::NoButton);
                    settle();
                    lane = panel.findChild<CurveLane *>("filterCurve0");
                    if (filters()[0].curve.keys.size() == n) at = QPointF(x, y);
                    else {
                        undo.undo();
                        settle();
                        lane = panel.findChild<CurveLane *>("filterCurve0");
                    }
                }
            check(at.x() >= 0, "its key can be taken");
            const QPointer<CurveLane> held(lane);
            mouse(lane, QEvent::MouseButtonPress, at, Qt::LeftButton);
            for (int k = 1; k <= 5; ++k) mouse(lane, QEvent::MouseMove, at + QPointF(-30 * k, 10 * k), Qt::LeftButton);
            check(held && held->isVisible(), "the curve dragged is not replaced while it is dragged");
            mouse(lane, QEvent::MouseButtonRelease, at + QPointF(-150, 50), Qt::NoButton);
            settle();
            const auto keys = filters()[0].curve.keys;
            check(keys.size() == 4 && keys[2].t < 0.9 && keys[2].v < 0.9, "its key moved where it was dragged");
        }
    }

    if (argc > 1) panel.grab().save(QString::fromLocal8Bit(argv[1]));
    std::printf("interaction_filters_ui: %d failures\n", failures);
    return failures ? 1 : 0;
}
