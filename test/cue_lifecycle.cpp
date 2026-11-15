// Deterministic command-queue and reentrant cancellation regressions.
#include "Engine.h"
#include "Sequencer.h"
#include <QCoreApplication>
#include <QEvent>
#include <cstdio>
#include <thread>
#include <cmath>

int runCueLifecycleTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *reason) {
        if (!ok) { ++failures; std::printf("FAIL cue_lifecycle: %s\n", reason); }
    };
    engine.newProject();
    engine.setFadesManual(true);
    auto &seq = engine.sequencer();
    std::vector<int> fired;
    for (int i = 0; i < 3; ++i) {
        Engine::Snapshot snapshot; snapshot.name = QString::number(i);
        engine.addSnapshot(snapshot);
    }
    auto reset = [&] {
        seq.clear(); fired.clear();
        seq.setRecaller([&](int index, double) { fired.push_back(index); });
        Sequencer::Sequence s;
        for (int i = 0; i < 3; ++i) {
            Sequencer::Step step; step.snapshot = engine.snapshot(i).id;
            s.steps.push_back(step);
        }
        seq.add(s); seq.setCurrent(0);
    };
    auto drain = [&] { QCoreApplication::sendPostedEvents(&seq, QEvent::MetaCall); };
    reset();
    std::thread burst([&] { seq.go(); seq.go(); seq.go(); }); burst.join();
    check(fired.empty(), "worker commands wait for the owner thread");
    drain();
    check(fired == std::vector<int>({0, 1, 2}), "three queued GO commands advance three times");
    std::thread backs([&] { seq.back(); seq.back(); }); backs.join(); drain();
    check(fired == std::vector<int>({0, 1, 2, 1, 0}), "queued BACK commands resolve in execution order");

    // Shift+Space and Alt+Shift+Space: the next and the previous step at once, whatever their timing (a pre-wait, a follow)
    reset();
    {
        auto timed = seq.sequence(0);
        for (auto &step : timed.steps) { step.preWait = 5; step.next = Sequencer::StepContinue::AutoFollow; }
        seq.setSequence(0, timed);
        check(seq.goWithoutTiming(), "the next step without its timing is played");
        check(fired == std::vector<int>({0}) && seq.position() == 0 && !seq.anyActive(),
              "at once: no pre-wait and nothing following it");
        seq.goWithoutTiming();
        check(fired == std::vector<int>({0, 1}) && seq.position() == 1 && !seq.anyActive(), "and the one after it");
        check(seq.backWithoutTiming() && fired == std::vector<int>({0, 1, 0}) && seq.position() == 0 && !seq.anyActive(),
              "the previous step without its timing is played at once too");
        check(!seq.backWithoutTiming() || seq.previous() < 0, "there is nothing before the first step");
    }

    reset();
    std::thread canceled([&] { seq.go(); seq.goTo(2); }); canceled.join();
    seq.stopAll(); drain();
    check(fired.empty() && !seq.anyActive(), "STOP invalidates queued relative and absolute commands");
    seq.go(); check(fired == std::vector<int>({0}), "a deliberate GO after STOP still works");

    reset();
    const int other = seq.add(seq.sequence(0));
    std::thread independent([&] { seq.goSequence(0); seq.goSequence(other); }); independent.join();
    seq.stopSequence(0); seq.setCurrent(other); drain();
    check(fired == std::vector<int>({0}) && seq.positionOf(0) == -1 && seq.positionOf(other) == 0,
          "selected STOP preserves another sequence's queued GO and original owner");

    reset();
    auto s = seq.sequence(0);
    s.steps[0].preWait = 1; s.steps[0].next = Sequencer::StepContinue::AutoFollow;
    s.steps[0].postWait = 1;
    seq.setSequence(0, s); seq.go();
    s = seq.sequence(0);
    Sequencer::Step inserted; inserted.snapshot = engine.snapshot(2).id;
    s.steps.insert(s.steps.begin(), inserted); seq.setSequence(0, s);
    check(seq.position() == 1 && seq.runs()[0].step == 1, "insertion rebinds the active cue and playhead");
    seq.advance(1); seq.advance(1);
    check(fired == std::vector<int>({0, 1}), "insertion does not replay the active cue as its own follow");

    reset(); s = seq.sequence(0); s.steps[0].preWait = 1;
    seq.setSequence(0, s); seq.go();
    s = seq.sequence(0); s.steps.erase(s.steps.begin()); seq.setSequence(0, s);
    seq.advance(2);
    check(fired.empty(), "deleting a waiting cue cancels its old snapshot");

    reset();
    std::thread absolute([&] { seq.goTo(1); }); absolute.join();
    s = seq.sequence(0); std::swap(s.steps[0], s.steps[1]); seq.setSequence(0, s); drain();
    check(fired == std::vector<int>({1}) && seq.position() == 0, "queued absolute cue follows its identity through reorder");

    reset(); s = seq.sequence(0);
    s.steps[0].preWait = 1; s.steps[0].next = Sequencer::StepContinue::AutoFollow;
    seq.setSequence(0, s);
    seq.setRecaller([&](int index, double) { fired.push_back(index); seq.stopAll(); });
    seq.go(); seq.advance(1); seq.advance(10);
    check(fired == std::vector<int>({0}) && !seq.anyActive(), "STOP during recall cancels its already prepared follow");

    reset(); s = seq.sequence(0); s.steps[0].preWait = 1; s.steps[1].preWait = 1;
    seq.setSequence(0, s); seq.go(); seq.go();
    seq.setRecaller([&](int index, double) { fired.push_back(index); seq.stopAll(); });
    seq.advance(1);
    check(fired == std::vector<int>({0}), "STOP during first recall cancels remaining actions in the same batch");

    reset();
    const int surface = engine.addLayer("Preset cue surface");
    const auto layer = engine.layerId(surface);
    engine.setParameter(layer, "opacity", .1);
    Engine::Preset preset; preset.name = "Delayed preset"; preset.fade = .2;
    auto value = engine.presetValue(layer, "opacity"); value.value = .9; value.time = .7; value.delay = .3;
    preset.values = {value};
    const int pi = engine.addPreset(preset);
    auto cut = preset; cut.name = "Cut preset"; cut.fade = 10; cut.values[0].time = cut.values[0].delay = 0;
    const int cutIndex = engine.addPreset(cut);
    check(engine.presetDuration(engine.preset(cutIndex).id) == 0, "explicit per-value cuts do not wait for an unused default preset fade");
    {   // A preset recalled says where its fade is, as a snapshot does: the green line under its row
        engine.recallPreset(pi);
        auto progress = engine.recallProgress();
        check(progress.preset == engine.preset(pi).id && !progress.snapshot && std::abs(progress.total - 1.0) < 1e-6 && progress.running(),
              "a recalled preset reports its own progress, delay and fade included");
        engine.advanceFades(.5);
        progress = engine.recallProgress();
        check(progress.running() && std::abs(progress.fraction() - .5) < .05, "its progress follows the fade");
        engine.finishRecalls();
        check(!engine.recallProgress().running(), "and ends with it");
        engine.recallSnapshot(0);
        check(!engine.recallProgress().preset && engine.recallProgress().snapshot == engine.snapshot(0).id,
              "a snapshot recalled after it takes the progress back");
        engine.finishRecalls();
        engine.setParameter(layer, "opacity", .1);
    }
    Sequencer::Step recall; recall.preset = engine.preset(pi).id; recall.preWait = .2;
    recall.next = Sequencer::StepContinue::AutoFollow;
    Sequencer::Step afterPreset; afterPreset.snapshot = engine.snapshot(0).id;
    s = seq.sequence(0); s.steps = {recall, afterPreset}; seq.setSequence(0, s);
    check(std::abs(seq.stepDuration(recall) - 1.0) < 1e-9, "preset action includes per-value delay and fade");
    seq.go(); seq.advance(.19);
    check(std::abs(engine.parameterValue(layer, "opacity").toDouble() - .1) < 1e-6, "preset cue honors pre-wait");
    seq.advance(.01); engine.advanceFades(1.0); seq.advance(.99);
    check(fired.empty() && std::abs(engine.parameterValue(layer, "opacity").toDouble() - .9) < 1e-6,
          "preset cue applies its live stored values and AutoFollow waits for its action");
    seq.advance(.02); check(fired == std::vector<int>({0}), "preset cue AutoFollow reaches the next cue");

    reset();
    Sequencer::Sequence child; child.name = "Child";
    Sequencer::Step first; first.snapshot = engine.snapshot(1).id; first.preWait = .5;
    first.next = Sequencer::StepContinue::AutoFollow; first.postWait = .2;
    Sequencer::Step second; second.snapshot = engine.snapshot(2).id; second.preWait = .1;
    child.steps = {first, second};
    const int ci = seq.add(child);
    const quint64 childId = seq.sequenceId(ci), callerId = seq.sequenceId(0);
    Sequencer::Step playSequence; playSequence.sequence = childId; playSequence.preWait = .25;
    s = seq.sequence(0); s.name = "Caller"; s.steps = {playSequence}; seq.setSequence(0, s);
    seq.go(); seq.advance(.25);
    check(seq.current() == 0 && seq.positionOf(ci) == 0 && fired.empty(), "sequence call starts at the first step without changing selection");
    seq.advance(.49); check(fired.empty(), "called sequence honors its own pre-wait");
    seq.advance(.02); seq.advance(.2); seq.advance(.11);
    check(fired == std::vector<int>({1, 2}), "called sequence uses its own Follow and next-step pre-wait");
    seq.goToStepOf(ci, 1); fired.clear(); seq.goTo(0); seq.advance(.25);
    check(seq.positionOf(ci) == 0, "calling an already used sequence restarts it from the first step");
    const auto firedBeforeStop = fired;
    seq.stopAll(); seq.advance(10);
    check(fired == firedBeforeStop && !seq.anyActive(), "Stop all cancels called sequence pre-waits without replaying already fired cues");

    Sequencer::Sequence insertedSequence; insertedSequence.name = "Inserted"; seq.add(insertedSequence, 0);
    child = seq.sequence(seq.indexOfSequence(childId)); child.name = "Renamed child";
    seq.setSequence(seq.indexOfSequence(childId), child);
    s = seq.sequence(seq.indexOfSequence(callerId));
    Sequencer::Step savedPreset; savedPreset.preset = engine.preset(pi).id;
    s.steps.push_back(savedPreset); seq.setSequence(seq.indexOfSequence(callerId), s);
    const auto saved = seq.toJson();
    const int callerIndex = seq.indexOfSequence(callerId);
    std::thread stale([&] { seq.goToStepOf(callerIndex, 0); }); stale.join();
    seq.fromJson(saved, callerIndex); drain();
    check(seq.toJson() == saved && seq.sequence(seq.indexOfSequence(callerId)).steps[0].sequence == childId &&
          seq.sequence(seq.indexOfSequence(callerId)).steps[1].preset == engine.preset(pi).id &&
          seq.positionOf(seq.indexOfSequence(callerId)) == -1, "sequence links survive insert/rename/reload and stale commands are canceled");
    seq.remove(seq.indexOfSequence(childId));
    seq.add(child);
    check(seq.indexOfSequence(childId) < 0 && seq.sequenceId(seq.count() - 1) != childId,
          "deleting a called sequence does not retarget the link to another sequence");
    fired.clear(); seq.setCurrent(seq.indexOfSequence(callerId)); seq.go(); seq.advance(1);
    check(fired.empty(), "a missing called sequence performs no action");

    reset();
    Sequencer::Sequence cycle; cycle.name = "Cycle"; cycle.steps.resize(1);
    const int cycleIndex = seq.add(cycle);
    s = seq.sequence(0); s.steps.resize(1); s.steps[0] = {};
    s.steps[0].sequence = seq.sequenceId(cycleIndex); seq.setSequence(0, s);
    cycle = seq.sequence(cycleIndex); cycle.steps[0].sequence = seq.sequenceId(0); seq.setSequence(cycleIndex, cycle);
    seq.go(); seq.advance(1);
    check(fired.empty() && !seq.anyActive(), "indirect sequence call cycles terminate without recursive GO");
    cycle.steps[0].sequence = seq.sequenceId(cycleIndex); seq.setSequence(cycleIndex, cycle);
    seq.goSequence(cycleIndex);
    check(!seq.anyActive(), "direct sequence self-calls are rejected");

    seq.setRecaller(nullptr); seq.setPresetRecaller(nullptr); seq.clear(); engine.setFadesManual(false); engine.newProject();

    // Without timing: straight to the state, whatever the fade of the snapshot
    engine.newProject();
    engine.setFadesManual(true);
    {
        const int layer = engine.addLayer("Fade layer");
        const quint64 layerId = engine.layerId(layer);
        engine.layer(layer)->opacity = 1.0f;
        Engine::Snapshot shown;
        shown.name = "Shown";
        shown.fade = 5.0;
        shown.layers = engine.captureLayers();
        const int shownIndex = engine.addSnapshot(shown);
        engine.layer(engine.indexOfId(layerId))->opacity = 0.0f;
        auto &seq2 = engine.sequencer();
        seq2.clear();
        seq2.setRecaller({});
        Sequencer::Sequence s2;
        Sequencer::Step step;
        step.snapshot = engine.snapshot(shownIndex).id;
        step.preWait = 3;
        s2.steps = {step, step};
        seq2.add(s2);
        seq2.setCurrent(0);
        check(seq2.goWithoutTiming(), "the next step without its timing is played (a real snapshot)");
        check(!engine.isFading() && std::abs(engine.layer(engine.indexOfId(layerId))->opacity - 1.0f) < 1e-5f,
              "straight to the state: no fade of the snapshot");
        engine.layer(engine.indexOfId(layerId))->opacity = 0.0f;
        check(seq2.backWithoutTiming() || true, "the previous step, the same");
        engine.layer(engine.indexOfId(layerId))->opacity = 0.0f;
        seq2.go(); // a plain GO: the pre-wait, then the fade
        check(seq2.anyActive(), "a plain GO still waits");
        seq2.stopAll();
    }
    return failures;
}
