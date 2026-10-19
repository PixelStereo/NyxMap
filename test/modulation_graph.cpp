#include "Interactions.h"
#include <QCoreApplication>
#include <cmath>
#include <cstdio>
#include <limits>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char *what)
    {
        if (!ok)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", what);
        }
    };
    auto g = ModulationGraph::example();
    ModulationProcessor p;
    check(g.validate() && p.configure(g), "example compiles");
    auto audio = [](const ModulationNode &) { return .45; };
    auto value = p.step(0, audio);
    check(value && std::abs(*value - .455) < 1e-9, "audio + OSC multiplied by MIDI");
    p.feed(2, .3);
    p.feed(3, .8);
    value = p.step(1, audio);
    check(value && p.values()[3] == .75 && std::abs(p.values()[4] - .6) < 1e-9, "independent source events combine");
    p.feed(3, .1);
    p.step(0, audio);
    check(p.values()[5] == 1 && std::abs(p.values()[7] - p.values()[6]) < 1e-9, "low multiplier selects sine");
    p.feed(3, .21);
    p.step(0, audio);
    check(p.values()[5] == 1, "hysteresis holds sine");
    p.feed(3, .23);
    p.step(0, audio);
    check(p.values()[5] == 0, "hysteresis exits sine");
    p.feed(3, .19);
    p.step(0, audio);
    check(p.values()[5] == 0, "hysteresis holds primary");
    p.feed(3, .17);
    p.step(0, audio);
    check(p.values()[5] == 1, "hysteresis enters sine");
    check(!g.canConnect(5, 4, 0) && !g.canConnect(6, 4, 0) && !g.canConnect(3, 8, 0) && g.canConnect(2, 5, 1),
          "cycles and port types checked");
    p.feed(3, .8);
    p.feed(2, 2);
    p.step(0, audio);
    check(p.values()[3] > 1 && p.values()[4] > 1, "intermediates stay unclamped");
    for (int i = 0; i < 300; ++i)
        value = p.step(.016, audio);
    check(value && *value <= 1 && *value >= 0, "output clamp");
    auto it = Interaction::made(InteractionSource::Graph);
    it.graph = g;
    it.name = "Reactive Opacity";
    check(Interaction::fromJson(it.toJson()).toJson() == it.toJson(), "interaction and graph round trip");
    auto moved = g;
    moved.nodes[0].position = {100, 100};
    check(p.configure(moved), "move recompiles");
    p.step(0, audio);
    check(p.values()[2] == .8 && p.values()[1] == 2, "layout edit retains incoming MIDI and OSC");
    auto bad = g;
    bad.nodes[0].id = 2;
    check(!bad.validate() && !p.configure(bad) && !p.step(.016, audio), "invalid graph cannot emit");
    bad = g;
    bad.nodes[0].type = ModulationNode::Type(100);
    check(!bad.validate(), "unknown node rejected");
    bad = g;
    bad.nodes[0].value = std::numeric_limits<double>::quiet_NaN();
    check(!bad.validate(), "non-finite rejected");
    bad = g;
    bad.nodes[3].inputs[0] = 5;
    check(!bad.validate(), "saved cycle rejected");
    check(ModulationGraph::fromJson(g.toJson()).toJson() == g.toJson(), "graph serialization stable");
    p.configure(g);
    p.feed(3, .7);
    p.step(0, audio);
    p.feed(3, 0);
    value = p.step(.016, audio);
    check(value && *value > .455 && *value < .5, "handover is smoothed");
    for (int i = 0; i < 4000; ++i)
    {
        value = p.step(.016, audio);
        check(value && std::isfinite(*value) && *value >= 0 && *value <= 1, "finite bounded simulation");
    }
    std::printf("modulation_graph: %d failures\n", failures);
    return failures ? 1 : 0;
}
