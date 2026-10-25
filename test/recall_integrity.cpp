#include "Commands.h"
#include "Engine.h"
#include <QTemporaryDir>
#include <QUndoStack>
#include <cmath>
#include <cstdio>

int runRecallIntegrityTests(Engine &engine) {
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::printf("FAIL recall_integrity: %s\n", why);
        }
    };
    engine.newProject();
    engine.setFadesManual(true);
    engine.setCompositionSize(QSize(32, 32));
    engine.setViewportSize(engine.indexOfId(engine.mainViewportId()), QSize(32, 32));
    QTemporaryDir files;
    QImage red(32, 32, QImage::Format_RGBA8888), blue = red;
    red.fill(Qt::red);
    blue.fill(Qt::blue);
    const QString redPath = files.path() + "/red.png", bluePath = files.path() + "/blue.png";
    check(red.save(redPath) && blue.save(bluePath), "create source fixtures");
    const int row = engine.addLayer("Source");
    const quint64 id = engine.layerId(row);
    QString error;
    check(engine.setLayerImage(row, redPath, &error), "load original source");
    Engine::Snapshot snapshot;
    snapshot.name = "Red";
    snapshot.fade = 0;
    snapshot.layers = engine.captureLayers(true);
    const int si = engine.addSnapshot(snapshot);
    check(engine.setLayerImage(row, bluePath, &error), "load live source");
    {
        QUndoStack undo;
        undo.push(new cmd::RecallSnapshot(&engine, si));
        engine.renderFrame();
        check(engine.grabLayerSource(engine.indexOfId(id)).pixelColor(16, 16).red() > 250,
              "recall replaces the source");
        undo.undo();
        engine.renderFrame();
        check(engine.grabLayerSource(engine.indexOfId(id)).pixelColor(16, 16).blue() > 250,
              "Undo restores the previous source");
        undo.redo();
        engine.renderFrame();
        check(engine.grabLayerSource(engine.indexOfId(id)).pixelColor(16, 16).red() > 250,
              "Redo reinstalls the snapshot source");
    }
    auto delayed = engine.snapshot(si);
    for (int k = 0; k < delayed.layers.size(); ++k) {
        auto state = delayed.layers[k].toObject();
        auto params = state.value("params").toObject();
        params["opacity"] = .2;
        state["params"] = params;
        state["delay"] = QJsonObject{{"opacity", 2}};
        delayed.layers[k] = state;
    }
    engine.setSnapshot(si, delayed);
    engine.removeLayer(engine.indexOfId(id));
    {
        QUndoStack undo;
        undo.push(new cmd::RecallSnapshot(&engine, si));
        check(engine.indexOfId(id) >= 0, "recall recreates a deleted layer");
        undo.undo();
        check(engine.indexOfId(id) < 0, "Undo removes only the layer recreated by this recall");
        engine.advanceFades(4);
        check(engine.indexOfId(id) < 0 && !engine.isFading(), "Undo cancels delayed work owned by the recreated layer");
        undo.redo();
        engine.renderFrame();
        check(engine.indexOfId(id) >= 0, "Redo recreates the same stable layer identity");
        engine.advanceFades(3);
        check(std::abs(engine.parameterValue(id, "opacity").toDouble() - .2) < 1e-6,
              "Redo replays the captured parameter delay");
    }
    { // By id (the cue clock): a snapshot inserted before it, or removed, never makes another one recalled
        engine.newProject();
        Engine::Snapshot a, b;
        a.name = "A";
        b.name = "B";
        a.composition = b.composition = engine.captureComposition();
        const quint64 bId = engine.snapshot(engine.addSnapshot(b)).id;
        engine.addSnapshot(a, 0); // B moves from index 0 to index 1
        check(engine.recallSnapshotById(bId) && engine.recallProgress().snapshot == bId,
              "recall by id: the snapshot asked for, after one was inserted before it");
        check(engine.captureRecallLayersById(bId) == engine.captureRecallLayers(engine.indexOfSnapshot(bId)),
              "the undo capture by id is that snapshot's");
        engine.removeSnapshot(engine.indexOfSnapshot(bId));
        const quint64 before = engine.recallProgress().snapshot;
        check(!engine.recallSnapshotById(bId) && engine.recallProgress().snapshot == before,
              "recall by id of a snapshot removed: nothing recalled");
    }
    engine.newProject();
    engine.setFadesManual(false);
    return failures;
}
