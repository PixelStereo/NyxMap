#include "EngineInternal.h"
#include "VisionRuntime.h"
#include "MediaPipeWorkerScript.h"
#include <QProcess>
#include <QDir>
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
    return HandTracking::measure(frame, 0, input.handFeature, input.handConfidence, input.visionLandmark);
}
void Engine::releaseVision()
{
    if (m_vision) {
        m_vision->release();
        m_vision.reset();
    }
}
bool Engine::startMediaPipeInstall(QProcess &process) const
{
    QStringList searchPaths = qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
#ifdef Q_OS_MACOS
    searchPaths << QStringLiteral("/opt/homebrew/bin") << QStringLiteral("/usr/local/bin");
#endif
    QString python = qEnvironmentVariable("NYXMAP_MEDIAPIPE_PYTHON");
    if (python.isEmpty()) {
        for (const auto *name : {"python3.11", "python3.12", "python3.13", "python3", "python"}) {
            python = QStandardPaths::findExecutable(QLatin1String(name), searchPaths);
            if (!python.isEmpty()) break;
        }
    }
    if (python.isEmpty()) return false;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(python, {QStringLiteral("-c"), QString::fromUtf8(mediaPipeInstallerScript),
                           QStringLiteral("--directory"), handModelsDirectory()});
    return true;
}
QString Engine::handModelsDirectory() const
{
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
QJsonObject Engine::visionFaceExpressions(const PixelAnalysis::Settings &settings) const
{
    Lock lock(&m_mutex);
    if (!m_vision || m_visionReset || !settings.layer) return {};
    const auto frame = m_vision->hands(settings);
    if (!frame.timeNs || AudioStream::clockNs() - frame.completedNs > 500000000 ||
        !HandTracking::taskError(frame, HandTracking::Feature::FaceBlendshape).isEmpty())
        return {};
    const auto faces = frame.detections.value("face").toArray();
    if (faces.isEmpty()) return {};
    return faces.first().toObject().value("expressions").toObject();
}
QImage Engine::visionPreview(const PixelAnalysis::Settings &settings, bool hands) const
{
    Lock lock(&m_mutex);
    if (!m_vision || m_visionReset)
        return {};
    if (hands) {
        const auto frame = m_vision->hands(settings);
        if (!frame.preview.isNull())
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
