// An interaction switched to Action mode in its settings: its conditions and its "if true" / "if false" actions
// added, removed and undone; an action's target chosen from the show's values, its value edited by its kind.
#include "Engine.h"
#include "InteractionPanel.h"
#include "Widgets.h"
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
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
    auto check = [&](bool ok, const char *why) { if (!ok) { ++failures; std::printf("FAIL interaction_actions_ui: %s\n", why); } };
    InteractionPanel panel(&engine, &undo);
    panel.resize(720, 1000);
    panel.show();
    panel.learnFor(0, "opacity", InteractionSource::Keyboard);
    app.processEvents();
    auto current = [&] { return engine.interactions().back(); };
    auto settle = [&] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        app.processEvents();
    };

    auto *mode = panel.findChild<QComboBox *>("interactionMode");
    check(mode && mode->currentIndex() == 0, "a new interaction is a Mapping");
    if (!mode) { std::printf("interaction_actions_ui: %d failures\n", failures); return 1; }
    mode->setCurrentIndex(1);
    emit mode->activated(1);
    settle();
    check(current().isAction() && current().conditions.size() == 1, "Action mode, with a first condition");
    check(panel.findChild<QComboBox *>("conditionOp0") && panel.findChild<QLabel *>("actionState"), "its conditions and its state shown");

    if (auto *add = panel.findChild<QPushButton *>("addCondition")) add->click();
    settle();
    check(current().conditions.size() == 2, "+ Condition");
    undo.undo();
    settle();
    check(current().conditions.size() == 1, "adding a condition undoes");

    if (auto *op = panel.findChild<QComboBox *>("conditionOp0")) {
        op->setCurrentIndex(int(ConditionOp::InRange));
        emit op->activated(int(ConditionOp::InRange));
    }
    settle();
    check(current().conditions.front().op == ConditionOp::InRange, "an operator chosen");
    auto *upper = panel.findChild<QWidget *>("conditionB0");
    check(upper && upper->isVisible(), "a range shows its upper end");

    if (auto *add = panel.findChild<QPushButton *>("addTrueAction")) add->click();
    settle();
    check(current().onTrue.size() == 1 && current().onFalse.empty(), "+ Action if true");
    QAction *go = nullptr;
    if (auto *target = panel.findChild<QToolButton *>("trueTarget0"); target && target->menu()) {
        emit target->menu()->aboutToShow();
        for (QAction *owner : target->menu()->actions())
            if (owner->text() == "Composition" && owner->menu())
                for (QAction *cat : owner->menu()->actions())
                    if (cat->text() == "Show" && cat->menu())
                        for (QAction *a : cat->menu()->actions())
                            if (a->text() == "GO") go = a;
    }
    check(go != nullptr, "the show's GO offered as an action");
    if (go) go->trigger();
    settle();
    check(current().onTrue.size() == 1 && current().onTrue.front().param == "show/go", "GO chosen if true");

    // A switch: its value as Off / On
    Interaction edited = current();
    edited.onFalse = {InteractionConsequence{0, "show/blackout", 1}};
    engine.setInteraction(edited);
    settle();
    auto *value = panel.findChild<QComboBox *>("falseValue0");
    check(value && value->count() == 2 && value->currentIndex() == 1, "a switch set On");
    if (value) {
        value->setCurrentIndex(0);
        emit value->activated(0);
    }
    settle();
    check(current().onFalse.front().value == 0, "set Off instead");

    if (argc > 1) panel.grab().save(QString::fromLocal8Bit(argv[1])); // the Action's settings

    // Back to Mapping: the chain again, the Action's settings kept
    mode = panel.findChild<QComboBox *>("interactionMode");
    if (mode) {
        mode->setCurrentIndex(0);
        emit mode->activated(0);
    }
    settle();
    check(!current().isAction() && current().onTrue.size() == 1, "Mapping again, its actions kept for later");
    check(!panel.findChild<QComboBox *>("conditionOp0"), "no conditions shown in Mapping mode");

    std::printf("interaction_actions_ui: %d failures\n", failures);
    return failures ? 1 : 0;
}
