// An interaction in Action mode: the source's value tested against conditions (=, ≠, >, ≥, <, ≤, in / out of range,
// all or any), the "if true" actions done when the answer becomes true, the "if false" ones when it becomes false —
// a switch, a number, a show action. Saved with the project; a snapshot does not hold its definition.
// Standalone CTest area: nyxmap_tests --area interaction_actions.
#include "EngineInternal.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <cmath>
#include <cstdio>

int runInteractionActionTests(Engine &e, const QString &tmp)
{
    int failures = 0;
    auto check = [&](bool ok, const char *what) {
        if (ok) return;
        std::printf("FAIL interaction_actions: %s\n", what);
        ++failures;
    };
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    e.newProject();
    e.setFadesManual(true);
    auto layer = [&](const char *name) {
        const int i = e.addLayer(QString::fromLatin1(name));
        e.layer(i)->type = SourceType::Text; // no GPU source needed
        return e.layerId(i);
    };
    const quint64 a = layer("A"), b = layer("B");
    auto opacity = [&](quint64 id) { return e.parameterValue(id, "opacity").toDouble(); };
    auto visible = [&](quint64 id) { return e.layer(e.indexOfId(id))->enabled; };
    auto press = [&](const char *key, double v) {
        InteractionEvent ev;
        ev.source = InteractionSource::Keyboard;
        ev.address = QString::fromLatin1(key);
        ev.value = v;
        e.feedInteraction(ev);
    };

    // A key: pressed, B is hidden; let go, it shows again
    Interaction key = Interaction::made(InteractionSource::Keyboard);
    key.key = QStringLiteral("H");
    key.mode = InteractionMode::Action;
    key.conditions = {InteractionCondition{ConditionOp::GreaterOrEqual, 0.5, 1}};
    key.onTrue = {InteractionConsequence{b, QStringLiteral("enable"), 0}};
    key.onFalse = {InteractionConsequence{b, QStringLiteral("enable"), 1}};
    const quint64 keyId = e.addInteraction(key);
    e.renderFrame();
    check(visible(b), "nothing before the key");
    press("H", 1);
    e.renderFrame();
    check(!visible(b), "pressed: the action if true");
    press("H", 0);
    e.renderFrame();
    check(visible(b), "let go: the action if false");
    press("H", 1);
    press("H", 0);
    e.renderFrame();
    check(visible(b), "pressed and let go in one frame: both, in order");
    e.removeInteraction(keyId);

    // Internal: A's opacity in a range sets B's
    Interaction range = Interaction::made(InteractionSource::Internal);
    range.sourceLayer = a;
    range.sourceParam = QStringLiteral("opacity");
    range.mode = InteractionMode::Action;
    range.conditions = {InteractionCondition{ConditionOp::InRange, 0.2, 0.4}};
    range.onTrue = {InteractionConsequence{b, QStringLiteral("opacity"), 0.9}};
    range.onFalse = {InteractionConsequence{b, QStringLiteral("opacity"), 0.1}};
    check(e.setParameter(a, "opacity", 0.8) && e.setParameter(b, "opacity", 0.5), "set the opacities");
    const quint64 rangeId = e.addInteraction(range);
    e.renderFrame();
    check(near(opacity(b), 0.5), "false at first: nothing to undo");
    e.setParameter(a, "opacity", 0.3);
    e.renderFrame();
    check(near(opacity(b), 0.9), "in the range: if true");
    e.setParameter(b, "opacity", 0.5);
    e.setParameter(a, "opacity", 0.35);
    e.renderFrame();
    check(near(opacity(b), 0.5), "still true: nothing done again");
    e.setParameter(a, "opacity", 0.8);
    e.renderFrame();
    check(near(opacity(b), 0.1), "out of the range: if false");

    // Any of them: below 0.2 or above 0.7
    Interaction any = e.interactions().back();
    any.conditions = {InteractionCondition{ConditionOp::Less, 0.2, 0}, InteractionCondition{ConditionOp::Greater, 0.7, 0}};
    any.conditionsAll = false;
    check(e.setInteraction(any), "edit the conditions");
    e.setParameter(a, "opacity", 0.5);
    e.renderFrame();
    e.setParameter(a, "opacity", 0.9);
    e.renderFrame();
    check(near(opacity(b), 0.9), "any condition: one is enough");
    any.conditionsAll = true;
    check(e.setInteraction(any), "all of them");
    e.setParameter(a, "opacity", 0.5);
    e.renderFrame();
    e.setParameter(a, "opacity", 0.95);
    e.renderFrame();
    check(near(opacity(b), 0.1), "all conditions: never both");

    // Each operator
    auto is = [](ConditionOp op, double x, double v, double w = 0) { return InteractionCondition{op, v, w}.test(x); };
    check(is(ConditionOp::Equal, 3, 3) && !is(ConditionOp::Equal, 3.01, 3), "=");
    check(is(ConditionOp::NotEqual, 2, 3) && !is(ConditionOp::NotEqual, 3, 3), "≠");
    check(is(ConditionOp::Greater, 4, 3) && !is(ConditionOp::Greater, 3, 3), ">");
    check(is(ConditionOp::GreaterOrEqual, 3, 3) && !is(ConditionOp::GreaterOrEqual, 2, 3), "≥");
    check(is(ConditionOp::Less, 2, 3) && !is(ConditionOp::Less, 3, 3), "<");
    check(is(ConditionOp::LessOrEqual, 3, 3) && !is(ConditionOp::LessOrEqual, 4, 3), "≤");
    check(is(ConditionOp::InRange, 5, 8, 2) && !is(ConditionOp::InRange, 9, 2, 8), "in range (either order)");
    check(is(ConditionOp::OutOfRange, 9, 2, 8) && !is(ConditionOp::OutOfRange, 5, 2, 8), "out of range");

    // A locked layer is left alone
    e.setParameter(b, "opacity", 0.5);
    e.layer(e.indexOfId(b))->locked = true;
    e.setParameter(a, "opacity", 0.3);
    e.renderFrame();
    check(near(opacity(b), 0.5), "a locked layer is left alone");
    e.layer(e.indexOfId(b))->locked = false;

    // A show action: the blackout, done on the engine's thread
    any.conditions = {InteractionCondition{ConditionOp::Greater, 0.5, 0}};
    any.onTrue = {InteractionConsequence{0, QStringLiteral("show/blackout"), 1}};
    any.onFalse = {InteractionConsequence{0, QStringLiteral("show/blackout"), 0}};
    check(e.setInteraction(any), "a show action");
    e.setParameter(a, "opacity", 0.9);
    e.renderFrame();
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 30) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    check(e.blackout(), "the blackout, set by an action");
    e.setBlackout(false, 0);

    // Saved with its definition; a snapshot's state of it leaves that alone
    const Interaction back = Interaction::fromJson(any.toJson());
    check(back.isAction() && back.conditions == any.conditions && back.conditionsAll == any.conditionsAll &&
              back.onTrue == any.onTrue && back.onFalse == any.onFalse,
          "an Action read back as it was written");
    const QJsonObject state = any.stateJson();
    check(!state.contains("conditions") && !state.contains("if_true") && !state.contains("mode"), "a snapshot keeps its on / off, not its definition");
    QString err;
    const QString file = tmp + QStringLiteral("/interaction-actions.nyxmap");
    check(e.saveProject(file, {}, &err), "save the project");
    e.newProject();
    check(e.loadProject(file, nullptr, &err), "open it again");
    bool found = false;
    for (const Interaction &x : e.interactions())
        if (x.id == rangeId && x.isAction() && x.onTrue.size() == 1 && x.onTrue.front().param == QLatin1String("show/blackout"))
            found = true;
    check(found, "an Action saved with the project");

    e.setFadesManual(false);
    if (!failures) std::printf("interaction_actions: OK\n");
    return failures;
}
