// An interaction's filters in its settings, as a layer's effects: added from "+ Filter", each on or off, moved,
// removed, a Math filter's operation chosen, a Curve's editor shown — each one undo step. No Invert any more.
#include "Engine.h"
#include "AnimEditor.h"
#include "InteractionPanel.h"
#include "Widgets.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
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

    if (argc > 1) panel.grab().save(QString::fromLocal8Bit(argv[1]));
    std::printf("interaction_filters_ui: %d failures\n", failures);
    return failures ? 1 : 0;
}
