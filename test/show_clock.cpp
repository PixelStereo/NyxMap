// Show-clock regressions without a render context or wall-clock sleeps.
#include "Engine.h"
#include <QCoreApplication>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Engine engine;
    engine.setFadesManual(true);
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL show_clock: %s\n", why); }
    };

    // A late cue starts its timeline now. Time spent missing the pre-wait
    // deadline must not count as time already played by that timeline.
    Animation animation;
    animation.duration = 1;
    animation.loop = AnimLoop::Once;
    const int ai = engine.addAnimation(animation);
    const quint64 id = engine.animation(ai).id;
    Engine::Snapshot snapshot;
    snapshot.name = "After timeline";
    const int si = engine.addSnapshot(snapshot);
    int recalls = 0;
    auto &seq = engine.sequencer();
    seq.setRecaller([&](int) { ++recalls; });
    Sequencer::Step play, follow;
    play.timeline = id;
    play.preWait = .1;
    play.next = Sequencer::StepContinue::AutoFollow;
    play.postWait = .2;
    follow.snapshot = engine.snapshot(si).id;
    Sequencer::Sequence sequence;
    sequence.steps = {play, follow};
    seq.add(sequence);
    seq.setCurrent(0);
    seq.go();
    seq.advance(2); // misses the intended Play deadline by 1.9 seconds
    check(engine.animation(ai).state == AnimState::Playing, "late Play starts the timeline");
    check(recalls == 0 && seq.position() == 0, "late Play cannot consume AutoFollow before playing");
    engine.advanceFades(.75); seq.advance(.75);
    check(recalls == 0, "AutoFollow waits while the timeline plays");
    engine.advanceFades(.25); seq.advance(.25);
    check(recalls == 0, "AutoFollow preserves post-wait after the actual finish");
    seq.advance(.19);
    check(recalls == 0, "post-wait has not expired");
    seq.advance(.02);
    check(recalls == 1 && seq.position() == 1, "AutoFollow runs once after actual finish and post-wait");
    seq.clear();
    seq.setRecaller(nullptr);

    // Oscillators use continuous played time, independently of the pass clock
    // rebased by loop edits. Test both timeline and per-parameter animation.
    AnimTrack oscillator;
    oscillator.param = "opacity";
    oscillator.oscillator = true;
    oscillator.period = .7;
    oscillator.amplitude = .4;
    animation.loop = AnimLoop::Loop;
    animation.tracks = {oscillator};
    const int oi = engine.addAnimation(animation);
    const quint64 oscillatorId = engine.animation(oi).id;
    engine.controlAnimation(oscillatorId, AnimAction::Play);
    engine.advanceFades(3.11);
    auto value = [&] { return engine.parameterValue(0, "opacity").toDouble(); };
    const double before = value();
    engine.controlAnimation(oscillatorId, AnimAction::LoopMode, 0, AnimLoop::PingPong);
    engine.advanceFades(0);
    check(std::abs(value() - before) < 1e-10, "loop edit preserves oscillator phase");
    engine.advanceFades(.05);
    check(std::abs(value() - oscillator.valueAt(0, 3.16)) < 1e-10, "oscillator continues at the same speed");
    engine.controlAnimation(oscillatorId, AnimAction::Pause);
    const double paused = value();
    engine.controlAnimation(oscillatorId, AnimAction::LoopMode, 0, AnimLoop::Loop);
    engine.controlAnimation(oscillatorId, AnimAction::Play);
    check(std::abs(value() - paused) < 1e-10, "paused loop edit and resume preserve phase");
    engine.controlAnimation(oscillatorId, AnimAction::Seek, .2);
    check(std::abs(value() - oscillator.valueAt(0, .2)) < 1e-10, "explicit seek sets oscillator phase");
    engine.controlAnimation(oscillatorId, AnimAction::Rewind);
    check(std::abs(value() - oscillator.valueAt(0, 0)) < 1e-10, "rewind resets oscillator phase");
    engine.controlAnimation(oscillatorId, AnimAction::Stop);
    engine.controlAnimation(oscillatorId, AnimAction::Play);
    check(std::abs(value() - oscillator.valueAt(0, 0)) < 1e-10, "a fresh Play resets oscillator phase");
    engine.advanceFades(2.31);
    auto edited = engine.animation(oi);
    const double editValue = value();
    edited.loop = AnimLoop::PingPong;
    engine.setAnimation(oi, edited);
    engine.advanceFades(0);
    check(std::abs(value() - editValue) < 1e-10, "editing a timeline retains its runtime phase");
    engine.controlAnimation(oscillatorId, AnimAction::Stop);

    const int li = engine.addLayer("Oscillator");
    const quint64 layerId = engine.layerId(li);
    animation.tracks = {oscillator};
    engine.setLayerAnims(layerId, {animation});
    engine.advanceFades(3.11);
    const double layerBefore = engine.parameterValue(layerId, "opacity").toDouble();
    animation.loop = AnimLoop::PingPong;
    engine.setLayerAnims(layerId, {animation});
    engine.advanceFades(0);
    check(std::abs(engine.parameterValue(layerId, "opacity").toDouble() - layerBefore) < 1e-6,
          "per-parameter animation edits retain oscillator phase");
    engine.advanceFades(.05);
    check(std::abs(engine.parameterValue(layerId, "opacity").toDouble() - oscillator.valueAt(0, 3.16)) < 1e-6,
          "per-parameter oscillator continues after a loop edit");

    return failures ? 1 : 0;
}
