#include "Engine.h"
#include "ColorInfo.h"
#include "SourceColor.h"
#include "Commands.h"
#include "color_reference.h"
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
    const auto gray=colorReference::convert({.5,.5,.5},"srgb","linear709");
    check(std::abs(gray[0]-.21404114048223255)<1e-12, "reference matches the standard sRGB gray value");
    const auto p3red=colorReference::solve(colorReference::rgbToXyz(false),
        colorReference::multiply(colorReference::rgbToXyz(true),{1,0,0}));
    check(std::abs(p3red[0]-1.22494017628056)<1e-10 &&
          std::abs(p3red[1]+.0420569547096882)<1e-10,
          "reference derives known P3-to-Rec709 gamut coordinates from chromaticities");
    const auto gamma=colorReference::convert({.25,.25,.25},"linear709","rec709-g24");
    check(std::abs(gamma[0]-.5612310241546865)<1e-12, "reference uses the analytical Gamma 2.4 curve");
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
    const int copiedViewport = engine.addViewport("Copied color", {32,32});
    check(engine.applyLayerParts(copiedViewport, engine.layerJson(engine.indexOfId(id)), Engine::PartCompositing),
          "copy viewport compositing settings");
    check(engine.layer(copiedViewport)->outputSpace == "linear709" &&
          engine.layer(copiedViewport)->outputInputSpace == "displayp3" &&
          engine.layer(copiedViewport)->outputExposure == 10, "viewport paste carries output color settings");
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
    check(videoSourceColorKey(video).isEmpty(), "HDR video is never silently treated as an SDR profile");
    video.primaries = "bt709"; video.transfer = "bt709";
    check(videoSourceColorKey(video) == "rec709", "tagged Rec709 uses its own transfer, distinct from sRGB/gamma 2.4");
    video.primaries.clear();
    check(videoSourceColorKey(video).isEmpty(), "video Auto requires both primaries and transfer tags");
    Layer sourceLayer;
    sourceLayer.type = SourceType::Image;
    check(sourceColorStatus(sourceLayer).contains("assumed sRGB"), "untagged Auto reports its assumption");
    sourceLayer.imageProfile = QColorSpace::DisplayP3;
    check(sourceColorKey(sourceLayer) == "displayp3", "Auto recognizes the embedded P3 profile");
    sourceLayer.parameter("source/color_space")->setValue("linear709");
    const auto sourceParams = parametersToJson(sourceLayer.parameters());
    Layer restoredSource;
    restoredSource.type = SourceType::Image;
    parametersFromJson(restoredSource.parameters(), sourceParams);
    check(restoredSource.sourceColorSpace == "linear709", "source declarations survive parameter serialization");
    sourceLayer.type = SourceType::Layer;
    check(!sourceLayer.parameter("source/color_space") && sourceColorKey(sourceLayer) == "srgb",
          "layer taps cannot be decoded twice");
    sourceLayer.type = SourceType::Image;
    sourceLayer.sourceColorSpace = "auto";
    sourceLayer.imageProfile = QColorSpace::Bt2100Pq;
    check(sourceColorStatus(sourceLayer).contains("no tone mapping"), "HDR image status explicitly reports the limitation");
    sourceLayer.type = SourceType::Video;
    sourceLayer.video = std::make_unique<VideoDecoder>();
    auto frameLayout = std::make_shared<VideoLayout>();
    frameLayout->primaries = "bt709"; frameLayout->transfer = "bt709";
    sourceLayer.frame.layout = frameLayout;
    check(sourceColorKey(sourceLayer) == "rec709", "frame tags work even when the container has no color tags");
    frameLayout->primaries = "bt2020"; frameLayout->transfer = "smpte2084";
    check(sourceColorKey(sourceLayer) == "srgb" && sourceColorStatus(sourceLayer).contains("unsupported"),
          "a change to unsupported HDR frame tags is reported without an incorrect SDR conversion");
    return failures ? 1 : 0;
}
