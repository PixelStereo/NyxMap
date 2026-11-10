#include "EngineInternal.h"
#include "VisionRuntime.h"
#include <QSettings>
#include <QJsonArray>
#include <QStandardPaths>
#include <cmath>

void Engine::stepVision()
{
    if (m_visionReset) {
        releaseVision();
        m_visionReset = false;
    }
    std::vector<VisionRuntime::Request> requests;
    auto add = [&](const Interaction &input) {
        if (input.enabled && input.pixels.layer &&
            (input.source == InteractionSource::Pixels || input.source == InteractionSource::Hands))
            requests.push_back({input.pixels, input.source == InteractionSource::Hands ? HandTracking::taskFor(input.handFeature) : 0});
    };
    if (m_visionSetupInput && m_visionSetupInput->pixels.layer) {
        const auto &input = *m_visionSetupInput;
        const unsigned task = HandTracking::taskFor(input.handFeature);
        requests.push_back({input.pixels, (task <= 2 ? 3u : task) | HandTracking::setupPreviewTask});
    }
    for (const auto &input : m_interactions)
        if (input.enabled) {
            add(input);
            for (const auto &axis : input.xyInputs)
                add(axis);
        }
    for (const auto &node : m_interactionGraph.nodes)
        if (node.kind == InteractionGraph::Kind::Source) add(node.settings);
    for (const auto &layer : m_layers) {
        if (layer->type != SourceType::Layer || !isMediaPipeTap(layer->sourceTap) ||
            !layer->sourceLayer || !layer->needed) continue;
        PixelAnalysis::Settings settings;
        settings.layer = layer->sourceLayer;
        settings.tap = 2;
        static_assert(int(LayerTap::MediaPipePartsForeground) - int(LayerTap::MediaPipeMask) + 1 == HandTracking::pictureCount);
        const auto channel = size_t(int(layer->sourceTap) - int(LayerTap::MediaPipeMask));
        requests.push_back({settings, HandTracking::pictureTasks[channel], 1u << channel});
    }
    if (!m_vision && !requests.empty())
        m_vision = std::make_unique<VisionRuntime>([this] { drawQuad(); }, handModelsDirectory());
    if (!m_vision)
        return;
    m_vision->step(
        requests,
        [this](const PixelAnalysis::Settings &settings) -> VisionRuntime::Texture {
            if (!settings.layer)
                return {};
            const auto *source = layer(indexOfId(settings.layer));
            if (!source)
                return {};
            if (settings.tap <= 1)
                return {source->preFxTex, source->preFxW, source->preFxH};
            return {source->finalTex, source->finalW, source->finalH};
        },
        AudioStream::clockNs());
}
GLuint Engine::segmentationTexture(quint64 source, LayerTap tap, int &width, int &height) const
{
    if (!m_vision || m_visionReset) return 0;
    PixelAnalysis::Settings settings;
    settings.layer = source;
    settings.tap = 2;
    const auto mask = m_vision->segmentation(settings, int(tap) - int(LayerTap::MediaPipeMask), AudioStream::clockNs());
    width = mask.width;
    height = mask.height;
    return mask.id;
}
double Engine::visionValue(const Interaction &input)
{
    if (!m_vision || m_visionReset || !input.pixels.layer)
        return 0;
    const auto now = AudioStream::clockNs();
    if (input.source == InteractionSource::Pixels)
        return m_vision->pixelValue(input.pixels, input.pixelFeature, now);
    const auto frame = m_vision->hands(input.pixels);
    if (!HandTracking::taskError(frame, input.handFeature).isEmpty() || !frame.timeNs || now - frame.completedNs > 500000000)
        return 0;
    return HandTracking::measureSelected(frame, input.handIndex, input.handFeature, input.handConfidence,
                                         input.visionLandmark);
}
void Engine::releaseVision()
{
    if (m_vision) {
        m_vision->release();
        m_vision.reset();
    }
}
QString Engine::handModelsDirectory() const
{
    if (const auto bundled = HandTracking::bundledDirectory(); !bundled.isEmpty()) return bundled;
    return QSettings()
        .value(QStringLiteral("vision/handModels"),
               QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/models/hands"))
        .toString();
}
void Engine::setHandModelsDirectory(const QString &directory)
{
    QSettings().setValue(QStringLiteral("vision/handModels"), directory);
    Lock lock(&m_mutex);
    if (m_vision)
        m_vision->setModelsDirectory(directory);
}
void Engine::setVisionSetupInput(std::optional<Interaction> input)
{
    Lock lock(&m_mutex);
    if (input) input->enabled = true; // setup is independent of the interaction's output gate
    m_visionSetupInput = std::move(input);
}
QJsonObject Engine::visionSignals(const Interaction &input) const
{
    Lock lock(&m_mutex);
    if (!m_vision || m_visionReset || !input.pixels.layer) return {};
    const auto frame = m_vision->hands(input.pixels);
    if (!frame.timeNs || AudioStream::clockNs() - frame.completedNs > 500000000 ||
        !HandTracking::taskError(frame, input.handFeature).isEmpty())
        return {};

    using Feature = HandTracking::Feature;
    const auto task = HandTracking::taskFor(input.handFeature);
    QJsonObject values;
    if (task == HandTracking::taskFor(Feature::FaceBlendshape)) {
        const auto faces = frame.detections.value("face").toArray();
        if (!faces.isEmpty()) values = faces.first().toObject().value("expressions").toObject();
    }
    static const auto names = HandTracking::featureNames();
    for (int i = 0; i < names.size(); ++i) {
        const auto feature = Feature(i);
        const auto featureTask = HandTracking::taskFor(feature);
        if (featureTask != task && !(task <= 2 && featureTask <= 2)) continue;
        if (!HandTracking::taskError(frame, feature).isEmpty()) continue;
        const bool indexed = (feature >= Feature::HandPointX && feature <= Feature::PoseVisibility) ||
                             feature == Feature::FaceTransform;
        if (indexed && feature != input.handFeature) continue;
        const auto name = indexed ? QStringLiteral("%1 [%2]").arg(names[i]).arg(input.visionLandmark) : names[i];
        auto measure = [&](int slot) {
            return slot < 0 ? 0 : HandTracking::measure(frame, slot, feature, input.handConfidence, input.visionLandmark);
        };
        if (task <= 2 && feature != Feature::Count) {
            values.insert(QStringLiteral("Left · ") + name, measure(HandTracking::handSlot(frame, 2)));
            values.insert(QStringLiteral("Right · ") + name, measure(HandTracking::handSlot(frame, 3)));
        } else {
            values.insert(name, measure(0));
        }
    }
    return values;
}
QImage Engine::visionPreview(const PixelAnalysis::Settings &settings, bool hands) const
{
    Lock lock(&m_mutex);
    if (!m_vision || m_visionReset)
        return {};
    if (hands) {
        const auto frame = m_vision->hands(settings);
        if (!frame.preview.isNull() && AudioStream::clockNs() - frame.completedNs <= 500000000)
            return frame.preview;
    }
    return m_vision->preview(settings);
}
QString Engine::visionStatus(const PixelAnalysis::Settings &settings, bool hands) const
{
    Lock lock(&m_mutex);
    if (!settings.layer)
        return QStringLiteral("Choose an image layer to analyze, before or after FX");
    if (!m_vision || m_visionReset)
        return QStringLiteral("Waiting for camera / image frames");
    if (!hands)
        return m_vision->status(settings);
    const auto frame = m_vision->hands(settings);
    if (!frame.error.isEmpty())
        return frame.error;
    if (!frame.timeNs)
        return QStringLiteral("Waiting for MediaPipe detection");
    if (AudioStream::clockNs() - frame.completedNs > 500000000)
        return QStringLiteral("No recent camera frames");
    const auto results = frame.detections.keys();
    for (const auto &key : results)
        if (key.endsWith("_error")) return frame.detections.value(key).toString();
    QStringList delegates;
    for (const auto &key : results)
        if (key.endsWith("_delegate")) delegates << frame.detections.value(key).toString();
    delegates.removeDuplicates();
    return delegates.isEmpty() ? QStringLiteral("MediaPipe Tasks · local inference active")
                               : QStringLiteral("MediaPipe Tasks · %1").arg(delegates.join(" / "));
}
