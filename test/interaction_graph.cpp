#include "InteractionGraph.h"
#include <cstdio>
#include <chrono>
#include <cstring>
int main(int argc, char **argv)
{
    int failures = 0;
    auto check = [&](bool value, const char *reason) { if (!value) { ++failures; std::printf("FAIL: %s\n", reason); } };
    using Kind = InteractionGraph::Kind;
    InteractionGraph graph;
    Interaction filter;
    auto multiply = InteractionStage::made(InteractionFilter::Math);
    multiply.op = MathOp::Multiply;
    multiply.a = 2;
    filter.filters = {multiply};
    graph.nodes = {{1, Kind::Source, {}}, {2, Kind::Filter, filter}, {3, Kind::Destination, {}}, {4, Kind::Destination, {}}};
    check(graph.connect(1, 2) && graph.connect(2, 3) && graph.connect(2, 4), "shared filter fan-out");
    const size_t edges = graph.edges.size();
    check(!graph.connect(2, 2) && graph.edges.size() == edges, "cycle rejected atomically");
    check(!graph.connect(1, 3), "ambiguous destination rejected");
    check(!graph.connect(1, 2), "duplicate cable rejected");
    check(!graph.connect(999, 2), "missing endpoint rejected");
    InteractionGraphProcessor processor;
    check(processor.configure(graph), "valid graph compiles");
    processor.step(1.0 / 60);
    check(!processor.value(3), "uninitialised source does not write");
    processor.input(1, .25);
    processor.step(1.0 / 60);
    check(processor.value(3) == .5 && processor.value(4) == .5, "shared result reaches both destinations");
    graph.nodes.push_back({5, Kind::Source, {}});
    graph.nodes.push_back({6, Kind::Add, {}});
    graph.nodes.push_back({7, Kind::Destination, {}});
    check(graph.connect(2, 6) && graph.connect(5, 6) && graph.connect(6, 7), "explicit combination");
    check(processor.configure(graph), "compile combination");
    processor.input(1, .25); processor.input(5, .1); processor.step(.01);
    check(processor.value(7) && std::abs(*processor.value(7) - .6) < 1e-9, "sum is explicit");
    InteractionGraph restored;
    check(InteractionGraph::fromJson(graph.toJson(), &restored), "graph document round trip");
    check(restored.nodes.size() == graph.nodes.size() && restored.edges.size() == graph.edges.size(), "topology preserved");
    auto broken = graph.toJson(); broken["version"] = 99;
    check(!InteractionGraph::fromJson(broken, &restored) && restored.nodes.size() == graph.nodes.size(), "invalid load is atomic");
    // A shared temporal filter must advance once, not once per destination.
    graph.nodes[1].settings.filters = {InteractionStage::made(InteractionFilter::Damper)};
    InteractionProcessor reference;
    check(processor.configure(graph), "temporal graph compiles");
    for (int frame = 0; frame < 60; ++frame) {
        const double input = frame < 10 ? 0 : 1;
        processor.input(1, input);
        reference.input(graph.nodes[1].settings, input);
        auto expected = reference.step(graph.nodes[1].settings, 1.0 / 60);
        processor.step(1.0 / 60);
        check(processor.value(3) == expected && processor.value(4) == expected, "shared filter advances exactly once");
    }
    const auto held = processor.value(3);
    graph.nodes.push_back({100, Kind::Source, {}});
    check(processor.configure(graph) && processor.value(3) == held, "unrelated edit preserves history");
    InteractionGraph mixGraph;
    mixGraph.nodes = {{1, Kind::Source, {}}, {2, Kind::Source, {}}, {3, Kind::Mix, {}, .25}};
    check(mixGraph.connect(1, 3) && mixGraph.connect(2, 3), "mix accepts two inputs");
    for (auto kind : {Kind::Add, Kind::Multiply, Kind::Minimum, Kind::Maximum, Kind::Mix, Kind::Select}) {
        mixGraph.nodes[2].kind = kind;
        check(processor.configure(mixGraph), "combiner compiles");
        processor.input(1, 2); processor.input(2, 4); processor.step(.01);
        const double expected = kind == Kind::Add ? 6 : kind == Kind::Multiply ? 8 : kind == Kind::Maximum ? 4 : kind == Kind::Mix ? 2.5 : 2;
        check(processor.value(3) == expected, "combiner output uses raw units");
    }
    mixGraph.nodes[2].kind = Kind::Select;
    mixGraph.nodes[2].selectedInput = 1;
    mixGraph.edges.erase(mixGraph.edges.begin());
    check(processor.configure(mixGraph), "disconnected input retains port identity");
    processor.input(2, 4); processor.step(.01);
    check(processor.value(3) == 4, "B remains B after removing A");
    mixGraph.edges = {{1, 3, 0}};
    mixGraph.nodes[2].selectedInput = 0;
    check(processor.configure(mixGraph), "selector with A only compiles");
    processor.input(1, 7); processor.step(.01);
    check(processor.value(3) == 7, "A does not require a connected B");
    mixGraph.nodes[2].selectedInput = 1;
    check(processor.configure(mixGraph), "select disconnected B");
    processor.step(.01);
    check(!processor.value(3), "unconnected selection produces no stale signal");
    InteractionGraph toggle;
    Interaction toggleSettings;
    toggleSettings.filters = {InteractionStage::made(InteractionFilter::Toggle)};
    toggle.nodes = {{1, Kind::Source, {}}, {2, Kind::Filter, toggleSettings}, {3, Kind::Destination, {}}};
    check(toggle.connect(1, 2) && toggle.connect(2, 3) && processor.configure(toggle), "event graph compiles");
    for (double value : {1., 0., 1., 0.}) {
        processor.clearEvent(); processor.input(1, value); processor.step(0, true);
    }
    processor.step(1. / 60);
    check(processor.value(3) == 0., "two presses in one frame toggle twice");
    if (!failures && argc > 1 && std::strcmp(argv[1], "--benchmark") == 0) {
        InteractionGraph load;
        load.nodes.push_back({1, Kind::Source, {}});
        quint64 next = 2;
        for (int branch = 0; branch < 256; ++branch) {
            Interaction settings;
            settings.filters = {InteractionStage::made(InteractionFilter::Damper)};
            const auto filter = next++;
            load.nodes.push_back({filter, Kind::Filter, settings});
            load.edges.push_back({1, filter});
            for (int target = 0; target < 4; ++target) {
                load.nodes.push_back({next, Kind::Destination, {}});
                load.edges.push_back({filter, next++});
            }
        }
        processor.configure(load);
        for (int i = 0; i < 100; ++i) { processor.input(1, i % 2); processor.step(1. / 120); }
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < 10000; ++i) { processor.input(1, i % 2); processor.step(1. / 120); }
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count() / 10000;
        std::printf("Graph CPU benchmark: %zu nodes, 256 shared dampers, 1024 destinations, %.3f us/step (10000 steps)\n", load.nodes.size(), us);
    }
    return failures ? 1 : 0;
}
