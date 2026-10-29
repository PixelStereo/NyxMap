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
    if (auto *up = panel.findChild<QToolButton *>("filterUp1")) up->click();
    settle();
    check(filters()[0].kind == InteractionFilter::Clamp && filters()[1].kind == InteractionFilter::Math, "Clamp moved before Math");
    undo.undo();
    settle();
    check(filters()[0].kind == InteractionFilter::Math, "the move undoes");
    if (auto *on = panel.findChild<QCheckBox *>("filterOn0")) on->setChecked(false);
    settle();
    check(!filters()[0].enabled, "a filter switched off");
    if (auto *remove = panel.findChild<QToolButton *>("filterRemove1")) remove->click();
    settle();
    check(filters().size() == 1 && filters()[0].kind == InteractionFilter::Math, "Clamp removed");
    undo.undo();
    settle();
    check(filters().size() == 2, "the removal undoes");

    check(add("Curve") && filters().size() == 3 && panel.findChild<CurveLane *>("filterCurve2"), "a Curve, with its editor");
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
            const QPointer<CurveLane> held(lane);
            const QPointF at(658, 14); // where a press takes the key at 0.999 (1.005), in a lane 690 wide
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
