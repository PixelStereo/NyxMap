#include "VisionRuntime.h"
#include "PixelSampler.h"
#include <QJsonDocument>
#include <algorithm>
#include <map>
#include <set>

namespace {
QString sourceKey(const PixelAnalysis::Settings &settings)
{
    return QString::number(settings.layer) + ':' + QString::number(settings.tap);
}
QImage imageOf(const std::vector<PixelAnalysis::Pixel> &pixels, int w, int h)
{
    if (w <= 0 || h <= 0 || pixels.size() != size_t(w) * h)
        return {};
    QImage image(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c)
                row[x * 4 + c] = uchar(std::clamp(pixels[size_t(y) * w + x][c], 0.f, 1.f) * 255 + .5f);
    }
    return image;
}
} // namespace
struct VisionRuntime::Private {
    struct Input {
        std::unique_ptr<PixelSampler> sampler;
        std::vector<QPointF> points;
        std::vector<PixelAnalysis::Pixel> pixels;
        std::map<quint64, qint64> pending;
        int width = 0, height = 0;
        quint64 received = 0;
        qint64 timeNs = 0, lastHandSubmit = 0;
        QString error;
    };
    struct Analysis {
        QString source;
        PixelAnalysis::Tracker tracker;
        PixelAnalysis::Values values;
        quint64 serial = 0;
        qint64 used = 0;
    };
    std::function<void()> drawQuad;
    HandTracking::Worker worker;
    std::map<QString, Input> inputs;
    std::map<QString, Analysis> analyses;
    QString error;
    quint64 serial = 0;
    explicit Private(std::function<void()> draw, QString models) : drawQuad(std::move(draw)), worker(std::move(models))
    {
    }
};
VisionRuntime::VisionRuntime(std::function<void()> drawQuad, QString directory)
    : d(std::make_unique<Private>(std::move(drawQuad), std::move(directory)))
{
}
VisionRuntime::~VisionRuntime() = default;
void VisionRuntime::release()
{
    for (auto &[key, input] : d->inputs)
        if (input.sampler)
            input.sampler->release();
    d->inputs.clear();
    d->analyses.clear();
}
void VisionRuntime::setModelsDirectory(QString directory) { d->worker.setDirectory(std::move(directory)); }
void VisionRuntime::step(const std::vector<Request> &requests,
                         const std::function<Texture(const PixelAnalysis::Settings &)> &texture, qint64 now)
{
    std::map<QString, Request> wanted;
    for (const auto &request : requests) {
        const QString key = sourceKey(request.settings);
        auto found = wanted.find(key);
        if (found == wanted.end())
            wanted[key] = request;
        else
            found->second.hands |= request.hands;
    }
    d->error = wanted.size() > 8 ? QStringLiteral("At most 8 visual analysis sources can run at once") : QString();
    auto invalidate = [&](const QString &key) {
        for (auto it = d->analyses.begin(); it != d->analyses.end();)
            if (it->second.source == key)
                it = d->analyses.erase(it);
            else
                ++it;
    };
    std::set<QString> active;
    int sources = 0;
    for (const auto &[key, request] : wanted) {
        if (sources++ >= 8)
            break;
        active.insert(key);
        auto &input = d->inputs[key];
        const auto picture = texture(request.settings);
        if (!picture.id || picture.width <= 0 || picture.height <= 0) {
            if (input.sampler)
                input.sampler->release();
            input.sampler.reset();
            input.width = input.height = 0;
            input.pending.clear();
            invalidate(key);
            input.pixels.clear();
            input.timeNs = 0;
            input.error = "Source has no current picture";
            continue;
        }
        const int width =
            request.hands ? std::max(16, int(320. * picture.width / std::max(picture.width, picture.height))) : 64;
        const int height =
            request.hands ? std::max(16, int(320. * picture.height / std::max(picture.width, picture.height))) : 64;
        if (input.width != width || input.height != height) {
            invalidate(key);
            if (input.sampler)
                input.sampler->release();
            input.sampler = std::make_unique<PixelSampler>(d->drawQuad);
            if (!input.sampler->initGl(&input.error)) {
                input.sampler.reset();
                continue;
            }
            input.width = width;
            input.height = height;
            input.timeNs = 0;
            input.pending.clear();
            input.pixels.clear();
            input.points.clear();
            input.points.reserve(size_t(width) * height);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    input.points.emplace_back((x + .5) / width, (y + .5) / height);
        }
        if (!input.sampler)
            continue;
        quint64 tag = 0;
        std::vector<PixelAnalysis::Pixel> pixels;
        while (input.sampler->collectRgba(&pixels, &tag)) {
            const auto timestamp = input.pending.find(tag);
            if (timestamp != input.pending.end() && pixels.size() == size_t(width) * height) {
                input.pixels = std::move(pixels);
                input.timeNs = timestamp->second;
                input.received = tag;
                input.error.clear();
                if (request.hands && input.timeNs - input.lastHandSubmit >= 66666666) {
                    d->worker.submit(key, imageOf(input.pixels, width, height), input.timeNs);
                    input.lastHandSubmit = input.timeNs;
                }
            }
            input.pending.erase(tag);
        }
        const quint64 serial = ++d->serial;
        input.pending[serial] = now;
        while (input.pending.size() > 8)
            input.pending.erase(input.pending.begin());
        input.sampler->sample(picture.id, input.points, serial, true); // never wait if the GPU ring is full
    }
    for (auto it = d->inputs.begin(); it != d->inputs.end();)
        if (!active.count(it->first)) {
            if (it->second.sampler)
                it->second.sampler->release();
            invalidate(it->first);
            it = d->inputs.erase(it);
        } else
            ++it;
    for (auto it = d->analyses.begin(); it != d->analyses.end();)
        if (now - it->second.used > 2000000000LL)
            it = d->analyses.erase(it);
        else
            ++it;
}
double VisionRuntime::pixelValue(const PixelAnalysis::Settings &settings, PixelAnalysis::Feature feature, qint64 now)
{
    auto input = d->inputs.find(sourceKey(settings));
    if (input == d->inputs.end() || input->second.timeNs == 0 || now - input->second.timeNs > 500000000)
        return 0;
    const QString key = QString::fromUtf8(QJsonDocument(settings.toJson()).toJson(QJsonDocument::Compact));
    if (!d->analyses.count(key) && d->analyses.size() >= 128)
        return 0;
    auto &analysis = d->analyses[key];
    auto &frame = input->second;
    analysis.used = now;
    analysis.source = sourceKey(settings);
    if (analysis.serial != frame.received) {
        analysis.values = analysis.tracker.update(frame.pixels, frame.width, frame.height, settings, frame.timeNs);
        analysis.serial = frame.received;
    }
    return analysis.values.value(feature);
}
HandTracking::Frame VisionRuntime::hands(const PixelAnalysis::Settings &settings) const
{
    const auto input = d->inputs.find(sourceKey(settings));
    return input == d->inputs.end() || !input->second.timeNs ? HandTracking::Frame{}
                                                             : d->worker.latest(sourceKey(settings));
}
QImage VisionRuntime::preview(const PixelAnalysis::Settings &settings) const
{
    auto found = d->inputs.find(sourceKey(settings));
    return found == d->inputs.end() ? QImage()
                                    : imageOf(found->second.pixels, found->second.width, found->second.height);
}
QString VisionRuntime::status(const PixelAnalysis::Settings &settings) const
{
    auto found = d->inputs.find(sourceKey(settings));
    if (found == d->inputs.end())
        return d->error.isEmpty() ? QStringLiteral("Waiting for image analysis") : d->error;
    return found->second.error.isEmpty() ? (found->second.timeNs ? QStringLiteral("Reading pixels asynchronously")
                                                                 : QStringLiteral("Waiting for GPU pixels"))
                                         : found->second.error;
}
