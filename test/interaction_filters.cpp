// An interaction's filters as a list (as a layer's effects): applied in their order, each on or off — the filters over
// time, Curve, Math, Clamp, Steps, Threshold, Delay, Speed; what was saved before (one filter, a curve, an inverted
// target range) read as such a list; each filter's settings reached as values (presets, snapshots, OSC).
// Standalone CTest area: nyxmap_tests --area interaction_filters.
#include "EngineInternal.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstdio>

int runInteractionFilterTests(Engine &e)
{
    int failures = 0;
    auto check = [&](bool ok, const char *what) {
        if (ok) return;
        std::printf("FAIL interaction_filters: %s\n", what);
        ++failures;
    };
    auto near = [](double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; };
    auto stage = [](InteractionFilter kind, double a = qQNaN(), double b = qQNaN(), MathOp op = MathOp::Multiply) {
        InteractionStage s = InteractionStage::made(kind);
        if (!std::isnan(a)) s.a = a;
        if (!std::isnan(b)) s.b = b;
        s.op = op;
        return s;
    };
    // One value through a chain (no time), the result before the master level's 0..1 clamp is the same as after
    auto through = [](const std::vector<InteractionStage> &filters, double x) {
        Interaction it = Interaction::made(InteractionSource::Osc);
        it.filters = filters;
        InteractionProcessor p;
        p.input(it, x);
        return *p.step(it, 1.0 / 60);
    };

    // Math, each operation on x with a
    auto math = [&](MathOp op, double a, double x) { return through({stage(InteractionFilter::Math, a, 0, op)}, x); };
    check(near(math(MathOp::Add, 0.25, 0.5), 0.75) && near(math(MathOp::Subtract, 0.25, 0.5), 0.25), "+ and −");
    check(near(math(MathOp::Multiply, 0.5, 0.8), 0.4) && near(math(MathOp::Divide, 4, 0.8), 0.2), "× and ÷");
    check(near(math(MathOp::Divide, 0, 0.8), 0.8), "÷ 0 leaves the value");
    check(near(math(MathOp::Modulo, 0.25, 0.6), 0.1) && near(math(MathOp::Power, 2, 0.5), 0.25), "modulo and power");
    check(near(math(MathOp::Min, 0.3, 0.6), 0.3) && near(math(MathOp::Max, 0.3, 0.1), 0.3), "min and max");
    check(near(math(MathOp::Round, 0.25, 0.4), 0.5) && near(math(MathOp::Floor, 0.25, 0.4), 0.25) &&
              near(math(MathOp::Ceil, 0.25, 0.3), 0.5), "floor, ceil, round to a step");
    check(near(math(MathOp::OneMinus, 0, 0.2), 0.8), "1 − x inverts");
    check(near(math(MathOp::Multiply, 3, 0.5), 1.0), "brought back within 0..1 at the end");

    // Order matters, and a value may leave 0..1 between filters
    check(near(through({stage(InteractionFilter::Math, 3, 0, MathOp::Multiply), stage(InteractionFilter::Math, 1.2, 0, MathOp::Subtract)}, 0.5), 0.3),
          "× 3 then − 1.2: 0.3 (1.5 kept between them)");
    check(near(through({stage(InteractionFilter::Math, 1.2, 0, MathOp::Subtract), stage(InteractionFilter::Math, 3, 0, MathOp::Multiply)}, 0.5), 0.0),
          "the other order: 0");
    InteractionStage off = stage(InteractionFilter::Math, 0, 0, MathOp::OneMinus);
    off.enabled = false;
    check(near(through({off}, 0.2), 0.2), "a filter off lets the value through");

    // Clamp, Steps, Curve
    check(near(through({stage(InteractionFilter::Clamp, 0.2, 0.6)}, 0.9), 0.6) && near(through({stage(InteractionFilter::Clamp, 0.2, 0.6)}, 0.1), 0.2),
          "clamp");
    check(near(through({stage(InteractionFilter::Steps, 3)}, 0.3), 0.5) && near(through({stage(InteractionFilter::Steps, 3)}, 0.2), 0.0),
          "three steps: 0, 0.5, 1");
    InteractionStage inverted = stage(InteractionFilter::Curve);
    inverted.curve.keys[0].v = 1;
    inverted.curve.keys[1].v = 0;
    check(near(through({inverted}, 0.25), 0.75), "a curve");

    // Threshold, with its margin: no flicker around it
    {
        Interaction it = Interaction::made(InteractionSource::Osc);
        it.filters = {stage(InteractionFilter::Threshold, 0.5, 0.2)};
        InteractionProcessor p;
        auto at = [&](double x) {
            p.input(it, x);
            return *p.step(it, 1.0 / 60);
        };
        check(at(0.3) == 0 && at(0.55) == 0 && at(0.65) == 1 && at(0.45) == 1 && at(0.35) == 0, "threshold with a margin");
    }
    // Delay: the value of half a second ago
    {
        Interaction it = Interaction::made(InteractionSource::Osc);
        it.filters = {stage(InteractionFilter::Delay, 0.5)};
        InteractionProcessor p;
        p.input(it, 0.2);
        p.step(it, 1.0 / 60);
        p.input(it, 0.9);
        double y = 0;
        for (int k = 0; k < 15; ++k) y = *p.step(it, 1.0 / 60); // 0.25 s
        check(near(y, 0.2), "still the value of before");
        for (int k = 0; k < 30; ++k) y = *p.step(it, 1.0 / 60); // 0.75 s
        check(near(y, 0.9), "then the new one, half a second later");
    }
    // Speed: how fast it moves (1 for the full range in a second)
    {
        Interaction it = Interaction::made(InteractionSource::Osc);
        it.filters = {stage(InteractionFilter::Speed, 1, 0.001)};
        InteractionProcessor p;
        double y = 0;
        for (int k = 0; k <= 30; ++k) { // 0 → 0.25 in half a second: half the full scale
            p.input(it, 0.25 * k / 30.0);
            y = *p.step(it, 1.0 / 60);
        }
        check(near(y, 0.5, 0.02), "half the full range per second");
        for (int k = 0; k < 30; ++k) y = *p.step(it, 1.0 / 60);
        check(y < 0.02, "still: no speed");
    }

    // Saved before: one filter, a curve, an inverted target range — the same list, the same response
    {
        QJsonObject old = Interaction::made(InteractionSource::Osc).toJson();
        old.remove("filters");
        old["filter_on"] = true;
        old["filter"] = "damper";
        old["filter_settings"] = QJsonArray{0.3, 0.7};
        old["curve_on"] = true;
        old["curve"] = QJsonArray{QJsonObject{{"t", 0}, {"v", 0}, {"curve", 0}}, QJsonObject{{"t", 1}, {"v", 0.5}, {"curve", 0}}};
        old["target_range"] = QJsonArray{1, 0};
        const Interaction read = Interaction::fromJson(old);
        check(read.filters.size() == 3 && read.filters[0].kind == InteractionFilter::Damper && near(read.filters[0].a, 0.3) &&
                  near(read.filters[0].b, 0.7) && read.filters[1].kind == InteractionFilter::Curve &&
                  read.filters[2].kind == InteractionFilter::Math && read.filters[2].op == MathOp::OneMinus,
              "a filter, a curve, an inverted range: three filters");
        check(near(read.dstMin, 0) && near(read.dstMax, 1), "the target range the right way up");
        old["filter_on"] = false;
        old["curve_on"] = false;
        old["target_range"] = QJsonArray{0, 1};
        check(Interaction::fromJson(old).filters.empty(), "none on: no filter");
        const Interaction again = Interaction::fromJson(read.toJson());
        check(again.filters == read.filters && near(again.dstMin, 0) && near(again.dstMax, 1), "written and read back as a list");
        // Pasted settings copied before (their filter, their curve) replace the filters
        Interaction target = Interaction::made(InteractionSource::Osc);
        target.filters = {stage(InteractionFilter::Clamp)};
        const Interaction pasted = Interaction::fromJson(Interaction::merged(target.toJson(), QJsonObject{{"filter_on", true}, {"filter", "average"}}));
        check(pasted.filters.size() == 1 && pasted.filters[0].kind == InteractionFilter::Average, "settings of before pasted");
    }
    // A snapshot of before: its filter values made a list
    {
        const QJsonObject upgraded = Engine::upgradedInteractionValues(
            QJsonObject{{"master", 1}, {"filter/enable", true}, {"filter/type", "attack_release"}, {"filter/a", 0.1}, {"curve/enable", false},
                        {"xy/y/filter/enable", false}});
        const QJsonArray list = QJsonDocument::fromJson(upgraded.value("filters").toString().toUtf8()).array();
        check(!upgraded.contains("filter/enable") && !upgraded.contains("filter/a") && upgraded.contains("master"), "its keys replaced");
        check(list.size() == 1 && list[0].toObject().value("type").toString() == "attack_release" &&
                  near(list[0].toObject().value("a").toDouble(), 0.1) &&
                  near(list[0].toObject().value("b").toDouble(), interactionFilterSetting(InteractionFilter::AttackRelease, 1).def),
              "the filter it held, its missing setting at its default");
        check(upgraded.contains("xy/y/filters") && QJsonDocument::fromJson(upgraded.value("xy/y/filters").toString().toUtf8()).array().isEmpty(),
              "its Y chain too");
    }

    // In the engine: each filter's settings are values (fade, preset, OSC), the list one value set at once
    e.newProject();
    e.setFadesManual(true);
    Interaction it = Interaction::made(InteractionSource::Osc);
    it.oscAddress = "/f";
    it.param = "opacity";
    it.filters = {stage(InteractionFilter::Math, 0.5, 0, MathOp::Multiply), stage(InteractionFilter::Clamp, 0, 1)};
    const quint64 id = e.addInteraction(it);
    const QString prefix = QStringLiteral("interaction/%1/").arg(id);
    ParamInfo info;
    check(e.parameterInfo(0, prefix + "filters/1/a", &info) && info.label.contains("Math") && e.parameterInfo(0, prefix + "filters/2/b", &info) &&
              !e.parameterInfo(0, prefix + "filters/1/b", nullptr) && e.parameterInfo(0, prefix + "filters/2/enable", nullptr),
          "each filter's settings as values");
    check(e.setParameter(0, prefix + "filters/1/a", 0.25) && near(e.interactions().back().filters[0].a, 0.25), "a setting written");
    const QString chain = QString::fromUtf8(QJsonDocument(Interaction::filtersToJson({stage(InteractionFilter::Steps, 5)})).toJson(QJsonDocument::Compact));
    check(e.setParameter(0, prefix + "filters", chain) && e.interactions().back().filters.size() == 1 &&
              e.interactions().back().filters[0].kind == InteractionFilter::Steps,
          "the list set at once");
    check(e.parameterInfo(0, prefix + "filters/1/a", &info) && info.label.contains("Steps") && !e.parameterInfo(0, prefix + "filters/2/a", nullptr),
          "its values made again");

    // A snapshot holds the list and each filter's values, and puts them back
    Engine::Snapshot shot;
    shot.name = "Filters";
    shot.fade = 0;
    shot.interactions = e.captureInteractions();
    const QJsonObject held = shot.interactions.at(0).toObject().value("params").toObject();
    check(held.contains("filters") && held.contains("filters/1/a") && held.contains("filters/1/enable"), "a snapshot holds them");
    const int si = e.addSnapshot(shot);
    Interaction changed = e.interactions().back();
    changed.filters = {stage(InteractionFilter::Average), stage(InteractionFilter::Delay)};
    e.setInteraction(changed);
    e.recallSnapshot(si);
    e.advanceFades(0.1);
    check(e.interactions().back().filters.size() == 1 && e.interactions().back().filters[0].kind == InteractionFilter::Steps &&
              near(e.interactions().back().filters[0].a, 5),
          "recalled: the list and its values");

    e.setFadesManual(false);
    if (!failures) std::printf("interaction_filters: OK\n");
    return failures;
}
