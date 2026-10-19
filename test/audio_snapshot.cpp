#include "Engine.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Engine engine;
    engine.setFadesManual(true);
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL audio_snapshot: %s\n", why); }
    };
    Interaction audio = Interaction::made(InteractionSource::Audio);
    audio.audioInput = true;
    audio.audioLow = 340;
    audio.audioHigh = 1240;
    audio.audioFeature = Interaction::AudioFeature::Onset;
    const quint64 id = engine.addInteraction(audio);
    const auto saved = engine.captureInteractions();
    const QJsonObject params = saved[0].toObject().value("params").toObject();
    check(params.contains("source/audio/low") && params.contains("source/audio/high") &&
          params.contains("source/audio/feature") && params.contains("source/audio/input"),
          "snapshot captures all four audio source controls");
    auto altered = engine.interactions()[0];
    altered.audioInput = false;
    altered.audioLow = 20;
    altered.audioHigh = 20000;
    altered.audioFeature = Interaction::AudioFeature::Level;
    engine.setInteraction(altered);
    engine.applyInteractions(saved, 0);
    auto restored = engine.interactions()[0];
    check(restored.id == id && restored.audioInput && restored.audioLow == 340 && restored.audioHigh == 1240 &&
          restored.audioFeature == Interaction::AudioFeature::Onset, "snapshot restores the selected input, measure and interval");
    engine.removeInteraction(id);
    engine.applyInteractions(saved, 0);
    restored = engine.interactions()[0];
    check(restored.id == id && restored.source == InteractionSource::Audio && restored.audioInput &&
          restored.audioLow == 340 && restored.audioHigh == 1240 && restored.audioFeature == Interaction::AudioFeature::Onset,
          "snapshot recreates a deleted Audio route with its complete source");
    return failures ? 1 : 0;
}
