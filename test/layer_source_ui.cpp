// The Source tab chooses another layer as this one's source — which layer, and where its picture is taken (its source
// as it is, before its FX, after them) — each choice one undo step; the layers that use this one are not offered.
#include "Engine.h"
#include "LayerInspector.h"
#include <QApplication>
#include <QComboBox>
#include <QTabWidget>
#include <QUndoStack>
#include <cstdio>

int runLayerSourceUiTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *reason) {
        if (!ok) { ++failures; std::printf("FAIL layer_source_ui: %s\n", reason); }
    };
    auto pump = [] {
        for (int k = 0; k < 3; ++k) {
            qApp->processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    };
    engine.setFadesManual(true);
    const quint64 a = engine.layerId(engine.addLayer("Picture"));
    engine.setLayerText(engine.indexOfId(a));
    const quint64 b = engine.layerId(engine.addLayer("Copy"));
    engine.setLayerText(engine.indexOfId(b));
    QUndoStack undo;
    LayerInspector inspector(&engine, &undo);
    inspector.resize(700, 1400);
    inspector.setLayer(engine.indexOfId(b));
    inspector.show();
    auto showSource = [&] {
        inspector.rebuild();
        for (auto *tabs : inspector.findChildren<QTabWidget *>("layerCategoryTabs"))
            for (int k = 0; k < tabs->count(); ++k)
                if (tabs->tabText(k) == "Source") tabs->setCurrentIndex(k);
        pump();
    };
    auto visible = [&](const char *name) -> QComboBox * {
        for (auto *c : inspector.findChildren<QComboBox *>(name))
            if (c->isVisible()) return c;
        return nullptr;
    };
    auto choose = [&](QComboBox *box, const QVariant &data) {
        const int k = box->findData(data);
        if (k < 0) return false;
        box->setCurrentIndex(k);
        emit box->activated(k);
        pump();
        showSource();
        return true;
    };
    auto layerOf = [&](quint64 id) { return engine.layer(engine.indexOfId(id)); };

    showSource();
    QComboBox *pick = visible("sourceLayer");
    check(pick && pick->currentIndex() == 0, "a text layer offers a layer as its source, none chosen");
    check(pick && pick->findData(QVariant::fromValue<qulonglong>(a)) > 0, "the other layer is offered");
    check(pick && pick->findData(QVariant::fromValue<qulonglong>(b)) < 0, "not itself");
    check(!visible("sourceTap"), "no tap before a layer is chosen");
    if (!pick) return failures;

    check(choose(pick, QVariant::fromValue<qulonglong>(a)), "choose the other layer");
    check(layerOf(b)->type == SourceType::Layer && layerOf(b)->sourceLayer == a && layerOf(b)->sourceTap == LayerTap::PostFx,
          "its picture, after its FX by default");
    QComboBox *tap = visible("sourceTap");
    check(tap && tap->count() == 3, "taken from its source, before its FX or after them");
    if (tap) check(choose(tap, int(LayerTap::Source)) && layerOf(b)->sourceTap == LayerTap::Source, "taken from its source");
    undo.undo();
    check(layerOf(b)->sourceTap == LayerTap::PostFx, "the tap undoes");
    undo.undo();
    check(layerOf(b)->type == SourceType::Text, "choosing the layer undoes");

    // A layer that uses this one is not offered to it (the picture would feed back on itself)
    inspector.setLayer(engine.indexOfId(a));
    showSource();
    pick = visible("sourceLayer");
    undo.redo(); // b uses a again
    showSource();
    pick = visible("sourceLayer");
    check(pick && pick->findData(QVariant::fromValue<qulonglong>(b)) < 0, "a layer using this one is not offered");

    engine.setFadesManual(false);
    if (!failures) std::printf("layer_source_ui: OK\n");
    return failures;
}
