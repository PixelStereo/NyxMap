#include "Engine.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <thread>
#include <cstdio>
int main(int argc,char **argv) {
    QSurfaceFormat format;format.setVersion(3,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QApplication app(argc,argv);
    Engine engine;QString error;if(!engine.initialize(&error)||!engine.start())return 1;
    const auto surfaceId=engine.layerId(engine.addLayer("Surface"));Engine::Snapshot snapshot;snapshot.name="Slow cue";snapshot.layers=engine.captureLayers();snapshot.fade=0;
    const int index=engine.addSnapshot(snapshot);const auto id=engine.snapshot(index).id;
    std::atomic<bool> inSlowRecall{false};int recorded=0,heartbeats=0;bool correctThread=false;
    QObject::connect(&engine,&Engine::snapshotRecalled,&engine,[&](int){inSlowRecall=true;QThread::msleep(120);inSlowRecall=false;},Qt::DirectConnection);
    engine.sequencer().setRecallRecorder([&](int cue,const Sequencer::RecallBefore &before){++recorded;correctThread=QThread::currentThread()==app.thread()&&cue==index&&!before.layers.isEmpty();});
    QTimer heartbeat;heartbeat.setInterval(1);QObject::connect(&heartbeat,&QTimer::timeout,[&]{if(inSlowRecall)++heartbeats;});heartbeat.start();
    const bool accepted=engine.sequencer().requestSnapshot(id);
    QElapsedTimer wait;wait.start();while(!recorded&&wait.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    int failures=0;
    auto check=[&](bool ok,const char *why){if(!ok){++failures;std::printf("FAIL cue_dispatch: %s\n",why);}};
    check(accepted,"snapshot dispatched to cue clock");check(recorded==1&&correctThread,"one undo record delivered on GUI thread");check(heartbeats>0,"GUI handles events during a slow recall");
    // A stale queued undo must not be delivered into a newly opened/cleared show.
    engine.sequencer().requestSnapshot(id);wait.restart();while(!inSlowRecall&&wait.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    if(inSlowRecall)engine.sequencer().clear();
    wait.restart();while(wait.elapsed()<250){app.processEvents();QThread::msleep(1);}
    check(recorded==1,"project generation invalidates pending undo delivery");
    // A request queued during an executing recall must not survive STOP.
    // Both operations happen in the direct callback: no timing-dependent race
    // between the test and the next cue-clock tick.
    std::atomic<int> stoppedRecalls{0};
    auto stopping=QObject::connect(&engine,&Engine::snapshotRecalled,&engine,[&](int){
        if(++stoppedRecalls==1){
            engine.sequencer().requestSnapshot(id);
            engine.sequencer().stopAll();
        }
    },Qt::DirectConnection);
    engine.sequencer().requestSnapshot(id);
    wait.restart();while(stoppedRecalls==0&&wait.elapsed()<5000){app.processEvents();QThread::msleep(1);}
    wait.restart();while(wait.elapsed()<350){app.processEvents();QThread::msleep(1);}
    check(stoppedRecalls==1,"STOP cancels snapshots queued during a recall");
    QObject::disconnect(stopping);
    // A delayed preset restores MIDI definitions on the cue clock. Port/timer
    // reconfiguration must be queued back to the Engine's QObject thread.
    Interaction midi = Interaction::made(InteractionSource::Midi);
    midi.layer = surfaceId; midi.param = "opacity";
    midi.id = engine.addInteraction(midi);
    auto *scan = engine.findChild<QTimer *>("midiInputScan");
    check(scan && scan->thread() == app.thread() && scan->isActive(), "MIDI scan belongs to the GUI thread");
    engine.removeInteraction(midi.id);
    check(scan && !scan->isActive(), "removing the last MIDI definition stops scanning");
    Engine::Preset preset; preset.name = "MIDI preset"; preset.fade = 0; preset.interactions = {midi};
    const auto presetId = engine.preset(engine.addPreset(preset)).id;
    engine.sequencer().clear();
    Sequencer::Sequence sequence; Sequencer::Step step; step.preset = presetId; step.preWait = .02;
    sequence.steps = {step}; engine.sequencer().add(sequence); engine.sequencer().go();
    wait.restart();
    while ((!scan || !scan->isActive() || engine.interactions().empty()) && wait.elapsed() < 5000) {
        app.processEvents(); QThread::msleep(1);
    }
    check(scan && scan->thread() == app.thread() && scan->isActive() && !engine.interactions().empty(),
          "a delayed preset safely restarts MIDI scanning on its owner thread");
    engine.sequencer().shutdownClock();
    const QString master = QStringLiteral("interaction/%1/master").arg(midi.id);
    check(engine.setParameter(0, master, 0.0), "interaction settings are available after preset recall");
    app.processEvents();
    // Keep the owner thread blocked while a producer changes settings. One
    // coalesced notification must arrive on the owner thread when events resume.
    std::atomic<int> notifications{0};
    std::atomic<bool> ownerThread{true}, writesOk{true};
    auto feedback = QObject::connect(&engine, &Engine::interactionValuesChanged, &engine, [&] {
        ++notifications;
        if (QThread::currentThread() != app.thread()) ownerThread = false;
    }, Qt::DirectConnection);
    std::thread producer([&] {
        for (int n = 1; n <= 64; ++n)
            if (!engine.setParameter(0, master, double(n) / 64)) writesOk = false;
    });
    producer.join();
    check(notifications == 0, "interaction feedback remains queued while the owner thread is blocked");
    wait.restart();
    while (notifications == 0 && wait.elapsed() < 5000) { app.processEvents(); QThread::msleep(1); }
    check(writesOk && notifications == 1 && ownerThread,
          "a burst of cross-thread interaction edits emits once on the owner thread");
    check(engine.parameterValue(0, master).toDouble() == 1.0, "coalesced feedback preserves the final interaction value");
    QObject::disconnect(feedback);
    engine.sequencer().setRecallRecorder(nullptr);engine.sequencer().shutdownClock();engine.shutdown();
    return failures?1:0;
}
