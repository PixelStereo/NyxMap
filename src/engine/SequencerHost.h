#pragma once
// What the sequencer (Sequencer.h) asks of the application that holds the show: the snapshots and presets a step recalls, the timelines
// it drives, the musical clock, and how the application runs (threaded, fades moved by hand in tests). An application gives one object
// that answers (NyxMap's Engine does); the sequencer never reaches the rest of the application. One without snapshots or presets leaves
// those answers as they are: no index, no duration, nothing recalled.
#include "Anim.h"
#include "BeatClock.h"

#include <QJsonObject>
#include <QtGlobal>

class SequencerHost
{
public:
    virtual ~SequencerHost() = default;

    // Snapshots: an index for an id (-1: none), its recall (with its fade, or `fadeOverride` seconds when >= 0), how long it takes
    virtual int indexOfSnapshot(quint64) const { return -1; }
    virtual void recallSnapshot(int, double) {}
    virtual bool recallSnapshotById(quint64, double) { return false; } // found and recalled under one lock
    virtual double snapshotDuration(quint64, double) const { return 0; }
    // What a recall of that snapshot replaces, for the interface's undo (opaque to the sequencer: given back to the recall recorder)
    virtual QJsonObject captureBeforeRecall(quint64) const { return {}; }
    // Presets, the same
    virtual int indexOfPreset(quint64) const { return -1; }
    virtual void recallPreset(int, double) {}
    virtual double presetDuration(quint64, double) const { return 0; }
    // Every recall on its way at its end now (fades, delayed values), not the timelines
    virtual void finishRecalls() {}

    // Timelines: what a step does to one (`value`: the seek time, or the speed), and how long Play plays it (infinity: frozen)
    virtual void controlAnimation(quint64 id, AnimAction action, double value, AnimLoop loop, int repeat) = 0;
    virtual double animationPlayTime(quint64 id) const = 0;
    virtual double animationRemaining(quint64 id) const = 0; // seconds left of what it plays now (0: it is not playing)

    // The show's musical clock: steps counted in beats and quantized
    virtual const BeatClock &beatClock() const = 0;

    // How the application runs: the frames on a thread of their own (else the waits move with advance()), the fades moved only by
    // hand (tests)
    virtual bool isThreaded() const { return false; }
    virtual bool fadesManual() const { return false; }
    // The pace of the show's time (1: real time; 0: paused): the waits move at it. Read from the cue clock's thread
    virtual double timeScale() const { return 1.0; }
    // A GO releases an emergency stop (Escape); a cue fired is an input that changes what is shown (the application's timings)
    virtual void resumeAfterStop() {}
    virtual void noteCueInput() {}
};
