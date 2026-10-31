// The show's musical clock, and what it does to a sequence: steps counted in beats, GO quantized to the beat or the bar.
#include "BeatClock.h"
#include "Engine.h"
#include <QGuiApplication>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

static void require(bool ok, const char *why)
{
    if (!ok) throw std::runtime_error(why);
}
static bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    try {
        // --- The clock as a function of time
        {
            BeatClock c;
            require(near(c.bpm(), 120) && c.beatsPerBar() == 4 && !c.running(), "defaults: 120 BPM, 4/4, stopped");
            c.setBpm(60);
            require(near(c.secondsPerBeat(), 1), "60 BPM is a beat a second");
            c.setBpm(5);
            require(near(c.bpm(), BeatClock::kMinBpm), "the tempo is bounded");
            c.setBpm(NAN);
            require(near(c.bpm(), BeatClock::kMinBpm), "a non-finite tempo is ignored");
            c.setBpm(120);
            require(near(c.secondsToNext(1), 0), "a stopped clock never quantizes");
            c.start();
            const int64_t t0 = BeatClock::nowNs();
            require(c.running() && c.beatAt(t0) < 0.01, "start: from the first beat");
            require(near(c.beatAt(t0 + 1'000'000'000), 2.0, 0.01), "120 BPM: two beats a second");
            // Quantize: from just after a beat, the next one is nearly a beat away
            const int64_t at = t0 + 100'000'000; // 0.2 beat in
            const double toBeat = c.secondsToNextAt(1, at), toBar = c.secondsToNextAt(4, at);
            require(toBeat > 0.35 && toBeat < 0.41, "to the next beat: 0.8 beat at 120 BPM");
            require(toBar > 1.85 && toBar < 1.95, "to the next bar: 3.8 beats");
            require(near(c.secondsToNextAt(0, at), 0), "no grid: no wait");
            const int64_t online = t0 + int64_t((1.0 - c.beatAt(t0)) * 0.5e9); // exactly on the next beat
            require(c.secondsToNextAt(1, online) < 1e-3, "on a line: now");
            c.setBpm(60); // the position goes on from where it is
            const double before = c.beat();
            require(c.beat() >= before, "a tempo change does not move the position back");
            c.stop();
            const double held = c.beat();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            require(near(c.beat(), held), "stopped: the position is kept");
            c.resume();
            require(c.running() && c.beat() >= held, "resume: from where it stopped");
            c.setBeatsPerBar(99);
            require(c.beatsPerBar() == 16, "the bar is bounded");
            c.setBeatsPerBar(3);
            const auto saved = c.toJson();
            BeatClock d;
            d.fromJson(saved);
            require(near(d.bpm(), 60) && d.beatsPerBar() == 3 && !d.running(), "bpm and bar are saved, not the running state");
        }
        // --- Tap tempo and an external clock
        {
            BeatClock c;
            for (int i = 0; i < 4; ++i) {
                c.tap();
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            require(c.bpm() > 200 && c.bpm() < 280, "four taps a quarter second apart: about 240 BPM");
            require(c.source() == BeatClock::Source::Internal, "taps are not an external source");
            c.sync(8.0, 90, true);
            require(c.source() == BeatClock::Source::External && near(c.bpm(), 90) && c.running() && near(c.beat(), 8.0, 0.05),
                    "sync: tempo, position and running state taken, source external");
            c.sync(NAN, 90, true);
            require(near(c.bpm(), 90), "a non-finite sync is ignored");
            c.sync(0, 500, false);
            require(near(c.bpm(), BeatClock::kMaxBpm) && !c.running(), "a sync is bounded too");
        }
        // --- A sequence in beats, quantized
        {
            Engine engine;
            engine.beatClock().setBpm(120); // a beat is half a second
            auto &sequencer = engine.sequencer();
            Sequencer::Sequence sequence;
            sequence.name = "Beats";
            sequence.beats = true;
            Sequencer::Step a, b;
            a.preWait = 2; // two beats: one second
            a.postWait = 4; // four beats: two seconds
            b.quantize = 4;
            sequence.steps = {a, b};
            sequencer.add(sequence);
            sequencer.setCurrent(0);
            require(sequencer.goTo(0), "GO");
            auto runs = sequencer.runs();
            require(runs.size() == 1 && near(runs[0].preWait, 1.0) && near(runs[0].postWait, 2.0), "waits in beats become seconds at the tempo");
            sequencer.stop();
            // The same in seconds
            auto seconds = sequence;
            seconds.beats = false;
            sequencer.setSequence(0, seconds);
            require(sequencer.goTo(0), "GO again");
            runs = sequencer.runs();
            require(runs.size() == 1 && near(runs[0].preWait, 2.0) && near(runs[0].postWait, 4.0), "in seconds they stay seconds");
            sequencer.stop();
            // Quantize waits for the bar of a running clock; a stopped clock does not hold it
            require(sequencer.goTo(1), "GO on the quantized step");
            runs = sequencer.runs();
            require(runs.empty() || near(runs[0].preWait, 0), "stopped clock: no grid, the step starts at once (nothing left waiting)");
            sequencer.stop();
            engine.beatClock().start();
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // just past the first beat of the bar
            require(sequencer.goTo(1), "GO on the quantized step, clock running");
            runs = sequencer.runs();
            require(runs.size() == 1 && runs[0].preWait > 1.8 && runs[0].preWait < 1.95, "it waits for the next bar (about 1.9 s at 120 BPM)");
            sequencer.stop();
            // Saved and read back
            const auto json = sequencer.toJson();
            Sequencer::Sequence again;
            again.name = "x";
            sequencer.fromJson(json, 0);
            require(sequencer.sequence(0).steps[1].quantize == 4 && !sequencer.sequence(0).beats, "quantize and the unit are saved");
            auto inBeats = sequencer.sequence(0);
            inBeats.beats = true;
            sequencer.setSequence(0, inBeats);
            sequencer.fromJson(sequencer.toJson(), 0);
            require(sequencer.sequence(0).beats, "the unit is read back");
            engine.shutdown();
        }
        std::cout << "Beat clock: tempo, quantize, tap, external sync, beats in sequences passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
