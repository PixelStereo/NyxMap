// Show-clock regressions without a render context or wall-clock sleeps.
#include "Engine.h"
#include <QCoreApplication>
#include <cmath>
#include <limits>
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

    for (const double hz : {60.0, 120.0}) {
        double wall = 0, show = 0;
        for (int i = 0; i < 600; ++i) {
            const double elapsed = i % 100 == 0 ? 3.1 : 1.0 / hz;
            wall += elapsed;
            const auto step = FrameStep::fromElapsed(elapsed, true);
            show += step.elapsed;
            check(step.shaderDelta <= .25, "shader integration step remains bounded");
        }
        check(std::abs(show - wall) < 1e-10, "repeated stalls do not accumulate show-clock drift at 60/120 Hz");
    }
    check(FrameStep::fromElapsed(8, false).elapsed == .25, "manual renders retain their bounded step");
    {
        FaderClock clock;
        clock.reset(1'900'000'000); // command near the end of a two-second frame
        check(std::abs(clock.step(2'000'000'000, true) - .1) < 1e-12,
              "a new fader excludes time before its command");
        check(std::abs(clock.step(2'500'000'000, true) - .5) < 1e-12,
              "the fader counts each subsequent wall interval once");
        clock.reset(2'550'000'000); // operator interrupts/reverses it
        check(std::abs(clock.step(2'600'000'000, true) - .05) < 1e-12,
              "a replacement fader starts at its own command");
        check(clock.step(5'000'000'000, false) == .25, "manual render faders retain the bounded step");
    }
    check(FrameStep::fromElapsed(-1, true).elapsed == 0 &&
          FrameStep::fromElapsed(std::numeric_limits<double>::infinity(), true).elapsed == 0,
          "invalid elapsed times cannot poison the clocks");

    engine.stopShowPlayback();
    check(engine.paused() && timeScale() == 0, "Escape holds the show clock");
    engine.setPaused(false);
    check(!engine.paused() && timeScale() == 1, "release Pause resumes rendering");
    engine.setPaused(true);
    engine.stopShowPlayback();
    Animation probe;
    const int probeIndex = engine.addAnimation(probe);
    engine.controlAnimation(engine.animation(probeIndex).id, AnimAction::Play);
    check(engine.paused(), "explicit Play never releases an operator Pause");
    engine.setPaused(false);
    engine.stopShowPlayback();
    engine.controlAnimation(engine.animation(probeIndex).id, AnimAction::Play);
    check(!engine.paused(), "explicit Play releases only the emergency hold");
    engine.controlAnimation(engine.animation(probeIndex).id, AnimAction::Stop);

    {
        Sequencer::Step waiting;
        waiting.timeline = engine.animation(probeIndex).id;
        waiting.preWait = 5;
        Sequencer::Sequence sequence;
        sequence.steps = {waiting};
        const int index = engine.sequencer().add(sequence);
        engine.stopShowPlayback();
        engine.sequencer().goSequence(index);
        check(!engine.paused(), "GO releases Escape before the pre-wait, so the wait can advance");
        engine.setPaused(true);
        engine.stopShowPlayback();
        engine.sequencer().goSequence(index);
        check(engine.paused(), "GO preserves an operator Pause");
        engine.sequencer().clear();
        engine.setPaused(false);
    }

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
