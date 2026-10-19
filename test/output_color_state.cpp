#include "Engine.h"
#include "ColorInfo.h"
#include "Commands.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QUndoStack>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Engine engine;
    const int viewport = engine.addViewport("Color output", {32,32});
    const quint64 id = engine.layerId(viewport);
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL output_color_state: %s\n", why); }
    };
    check(engine.parameterValue(id, "output/input_space") == "srgb" &&
          engine.parameterValue(id, "output/space") == "same" &&
          engine.parameterValue(id, "output/exposure").toDouble() == 0, "legacy projects default to unchanged output");
    QUndoStack undo;
    check(cmd::setValues(&undo, &engine, id,
          {{"output/input_space", "displayp3"}, {"output/space", "linear709"}, {"output/exposure", 1.25}},
          "Output color"), "output controls use ordinary undoable parameters");
    const auto saved = engine.layerJson(viewport).value("params").toObject();
    check(saved.value("output/input_space") == "displayp3" && saved.value("output/space") == "linear709" &&
          saved.value("output/exposure") == 1.25, "project serialization retains color settings");
    Layer restored;
    restored.isViewport = true;
    parametersFromJson(restored.parameters(), saved);
    check(restored.outputInputSpace == "displayp3" && restored.outputSpace == "linear709" &&
          restored.outputExposure == 1.25, "ordinary project parameter restore restores output color");
    undo.undo();
    check(engine.parameterValue(id, "output/space") == "same" &&
          engine.parameterValue(id, "output/exposure").toDouble() == 0, "one Undo restores the whole color edit");
    undo.redo();
    check(engine.parameterValue(id, "output/space") == "linear709", "Redo restores the chosen output space");
    check(!engine.setParameter(id, "output/space", "invalid"), "unknown output spaces are rejected");
    check(engine.setParameter(id, "output/exposure", 100) &&
          engine.parameterValue(id, "output/exposure") == 10, "exposure is bounded");
    const int normal = engine.addLayer("Not a viewport");
    check(engine.parameterValue(engine.layerId(normal), "output/space").isUndefined(), "output calibration is viewport-only");

    QTemporaryDir files;
    QImage image(16, 16, QImage::Format_RGBA64);
    image.fill(Qt::gray);
    check(imageColorDescription(image.colorSpace()).contains("Unknown"), "untagged images remain explicitly unknown");
    for (auto named : {QColorSpace::SRgb, QColorSpace::DisplayP3, QColorSpace::SRgbLinear}) {
        const QColorSpace profile(named);
        image.setColorSpace(profile);
        const QString path = files.path() + QStringLiteral("/%1.png").arg(int(named));
        check(image.save(path), "write tagged 16-bit PNG fixture");
        const QImage read(path);
        check(read.colorSpace() == profile, "real PNG profile survives decoding");
        const QString description = imageColorDescription(read.colorSpace());
        check(description.contains("primaries:") && description.contains("transfer:") && !description.contains("Unknown"),
              "tagged source details show primaries and transfer independently of depth");
    }
    VideoDecoder::Info video;
    video.matrix = "bt709";
    check(video.colorSpaceSummary().contains("Unknown primaries"), "a YUV matrix never implies primaries");
    video.primaries = "bt2020"; video.transfer = "smpte2084"; video.range = "limited (TV)";
    check(video.colorSpaceSummary().contains("bt2020") && video.colorSpaceSummary().contains("smpte2084") &&
          video.colorDescription().contains("limited"), "video tags expose HDR transfer, primaries and range without guessing");
    return failures ? 1 : 0;
}
