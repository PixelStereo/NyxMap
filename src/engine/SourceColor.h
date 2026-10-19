#pragma once
#include "ColorInfo.h"
#include "Layer.h"

// sRGB is the explicit SDR working space of the existing color tools and ISF
// effects. Decode only native producers: groups and layer taps already contain
// working RGB and must never be converted a second time.
inline QString videoSourceColorKey(const QString &primaries, const QString &transfer)
{
    if (primaries == QLatin1String("bt709")) {
        if (transfer == QLatin1String("iec61966-2-1")) return "srgb";
        if (transfer == QLatin1String("linear")) return "linear709";
        if (transfer == QLatin1String("bt709")) return "rec709";
    }
    if (primaries == QLatin1String("smpte432") && transfer == QLatin1String("iec61966-2-1"))
        return "displayp3";
    return {};
}
inline QString videoSourceColorKey(const VideoDecoder::Info &info) {
    return videoSourceColorKey(info.primaries, info.transfer);
}
inline VideoDecoder::Info effectiveVideoColorInfo(const Layer &layer) {
    auto info = layer.video ? layer.video->sourceInfo() : VideoDecoder::Info{};
    if (layer.frame.layout) {
        if (!layer.frame.layout->primaries.isEmpty()) info.primaries = layer.frame.layout->primaries;
        if (!layer.frame.layout->transfer.isEmpty()) info.transfer = layer.frame.layout->transfer;
    }
    return info;
}
inline QString sourceColorKey(const Layer &layer)
{
    if (layer.type == SourceType::Layer || layer.isGroup || layer.isViewport) return "srgb";
    if (layer.sourceColorSpace != QLatin1String("auto")) return layer.sourceColorSpace;
    if (layer.type == SourceType::Image) {
        const auto key = imageSourceColorKey(layer.imageProfile);
        return key.isEmpty() ? QStringLiteral("srgb") : key;
    }
    if (layer.video) {
        const auto &info = layer.video->sourceInfo();
        const auto *frame = layer.frame.layout.get();
        const auto key = videoSourceColorKey(frame && !frame->primaries.isEmpty() ? frame->primaries : info.primaries,
                                            frame && !frame->transfer.isEmpty() ? frame->transfer : info.transfer);
        if (!key.isEmpty()) return key;
    }
    return "srgb"; // explicitly reported as an assumption for unknown/unsupported sources
}
inline QString sourceColorStatus(const Layer &layer)
{
    if (layer.sourceColorSpace != QLatin1String("auto"))
        return QStringLiteral("Declared %1 → sRGB working space").arg(
            sourceColorSpaceLabels().value(sourceColorSpaceKeys().indexOf(layer.sourceColorSpace)));
    if (layer.type == SourceType::Image && layer.imageProfile.isValid()) {
        if (hdrImageColorSpace(layer.imageProfile))
            return QStringLiteral("HDR profile unsupported: no tone mapping; RGB left unchanged");
        return QStringLiteral("Embedded %1 → sRGB working space").arg(imageColorSpaceName(layer.imageProfile));
    }
    if (layer.video) {
        const auto info = effectiveVideoColorInfo(layer);
        if (!videoSourceColorKey(info).isEmpty())
            return QStringLiteral("Tagged %1 → sRGB working space").arg(info.colorSpaceSummary());
        return QStringLiteral("Unknown or unsupported video profile: RGB left unchanged; declare its SDR space if known");
    }
    if (layer.type == SourceType::Text) return QStringLiteral("Generated sRGB → sRGB working space");
    return QStringLiteral("Unknown profile: assumed sRGB; declare the source space if known");
}
