#include "VisionRuntime.h"
#include "PixelSampler.h"
#include "Gl.h"
#include <QJsonDocument>
#include <algorithm>
#include <cstring>
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
// The full-size path uses one byte-RGBA PBO per source. It never builds a list of per-pixel coordinates.
struct FrameReadback {
    GLuint fbo = 0, pbo = 0;
    GLsync fence = nullptr;
    QSize size;
    qint64 submitted = 0;

    void release()
    {
        auto f = gl();
        if (fence) f->glDeleteSync(fence);
        if (pbo) f->glDeleteBuffers(1, &pbo);
        if (fbo) f->glDeleteFramebuffers(1, &fbo);
        *this = {};
    }

    QImage collect(qint64 &timestamp)
    {
        if (!fence) return {};
        auto f = gl();
        const GLenum ready = f->glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        if (ready == GL_TIMEOUT_EXPIRED) return {};
        f->glDeleteSync(fence);
        fence = nullptr;
        if (ready == GL_WAIT_FAILED) return {};
        QImage image(size, QImage::Format_RGBA8888);
        if (image.isNull()) return {};
        GLint previous = 0;
        f->glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previous);
        f->glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        const auto bytes = GLsizeiptr(image.sizeInBytes());
        if (const auto *data = static_cast<const uchar *>(f->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT))) {
            for (int y = 0; y < size.height(); ++y)
                std::memcpy(image.scanLine(y), data + qsizetype(size.height() - 1 - y) * image.bytesPerLine(),
                            size_t(image.bytesPerLine()));
            if (!f->glUnmapBuffer(GL_PIXEL_PACK_BUFFER)) image = {};
        } else {
            image = {};
        }
        f->glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(previous));
        timestamp = submitted;
        return image;
    }

    void submit(const VisionRuntime::Texture &picture, qint64 now)
    {
        if (fence || now - submitted < 66666666) return;
        auto f = gl();
        if (!fbo) f->glGenFramebuffers(1, &fbo);
        if (!pbo) f->glGenBuffers(1, &pbo);
        GLint previousFbo = 0, previousPbo = 0, alignment = 0;
        f->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousFbo);
        f->glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPbo);
        f->glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
        f->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        f->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, picture.id, 0);
        if (f->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            f->glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
            const QSize dimensions(picture.width, picture.height);
            if (size != dimensions) {
                size = dimensions;
                f->glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(qint64(size.width()) * size.height() * 4), nullptr, GL_STREAM_READ);
            }
            f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
            f->glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            fence = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        }
        f->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        f->glPixelStorei(GL_PACK_ALIGNMENT, alignment);
        f->glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(previousPbo));
        f->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previousFbo));
        submitted = now;
    }
};
} // namespace
struct VisionRuntime::Private {
    struct Input {
        std::unique_ptr<PixelSampler> sampler;
        std::vector<QPointF> points;
        std::vector<PixelAnalysis::Pixel> pixels;
        std::map<quint64, qint64> pending;
        int width = 0, height = 0;
        quint64 received = 0;
        qint64 timeNs = 0;
        FrameReadback readback;
        QSize pictureSize;
        QString error;
        std::array<Texture2D, HandTracking::pictureCount> masks;
        std::array<qint64, HandTracking::pictureCount> maskTimes{}, maskReadyTimes{};
        void clearMasks() {
            for (auto &mask : masks) mask.destroy();
            maskTimes.fill(0);
            maskReadyTimes.fill(0);
        }
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
    for (auto &[key, input] : d->inputs) {
        if (input.sampler) input.sampler->release();
        input.clearMasks();
        input.readback.release();
    }
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
        else {
            found->second.tasks |= request.tasks;
            found->second.pictures |= request.pictures;
        }
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
            input.clearMasks();
            input.readback.release();
            input.error = "Source has no current picture";
            continue;
        }
        if (request.tasks) {
            const QSize dimensions(picture.width, picture.height);
            if (dimensions != input.pictureSize) {
                input.clearMasks();
                input.readback.release();
                input.pictureSize = dimensions;
            }
            qint64 timestamp = 0;
            QImage image = input.readback.collect(timestamp);
            if (!image.isNull() && image.size() == dimensions)
                d->worker.submit(key, std::move(image), timestamp, request.tasks);
            input.readback.submit(picture, now);
        } else if (input.readback.fbo) {
            input.readback.release();
        }
        constexpr int width = 64, height = 64;
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
            input.clearMasks();
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
            }
            input.pending.erase(tag);
        }
        if (request.pictures) {
            const auto frame = d->worker.latest(key);
            for (size_t i = 0; i < input.masks.size(); ++i) {
                if (!(request.pictures & (1u << i))) {
                    input.masks[i].destroy();
                    input.maskTimes[i] = 0;
                    input.maskReadyTimes[i] = 0;
                    continue;
                }
                const auto &mask = frame.pictures[i];
                if (!mask.isNull() && frame.preview.size() == QSize(picture.width, picture.height) &&
                    frame.timeNs > input.maskTimes[i] && now - frame.completedNs <= 500000000) {
                    input.masks[i].uploadImage(flippedVertically(mask));
                    input.maskTimes[i] = frame.timeNs;
                    input.maskReadyTimes[i] = frame.completedNs;
                }
            }
        } else {
            input.clearMasks();
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
            it->second.clearMasks();
            it->second.readback.release();
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
VisionRuntime::Texture VisionRuntime::segmentation(const PixelAnalysis::Settings &settings, int channel, qint64 nowNs) const
{
    const auto found = d->inputs.find(sourceKey(settings));
    if (found == d->inputs.end() || channel < 0 || channel >= int(found->second.masks.size())) return {};
    const auto &input = found->second;
    const auto time = input.maskReadyTimes[size_t(channel)];
    if (!time || nowNs - time > 500000000) return {};
    const auto &mask = input.masks[size_t(channel)];
    return {mask.tex, mask.w, mask.h};
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
