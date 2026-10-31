#include "Engine.h"
#include <QFileInfo>
#include <QGuiApplication>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const QString &why)
{
    if (!ok)
        throw std::runtime_error(why.toStdString());
}
static double mean(const QImage &image)
{
    require(!image.isNull(), "image readback");
    double sum = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            sum += image.pixelColor(x, y).redF();
    return sum / (image.width() * image.height());
}
int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    Engine engine;
    QString error;
    if (!engine.initialize(&error)) {
        std::cerr << error.toStdString();
        return 1;
    }
    try {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        QImage image(32, 32, QImage::Format_RGBA8888);
        image.fill(Qt::black);
        const auto black = dir.filePath("black.png"), white = dir.filePath("white.png"),
                   patch = dir.filePath("patch.png");
        require(image.save(black), "black fixture");
        image.fill(Qt::white);
        require(image.save(white), "white fixture");
        image.fill(Qt::black);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                image.setPixelColor(x, y, Qt::white);
        require(image.save(patch), "patch fixture");
        const auto root = QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath("../isf/effects/");
        auto setup = [&](const QString &name) {
            engine.newProject();
            engine.setCompositionSize(QSize(32, 32));
            const int layer = engine.addLayer("Camera image");
            require(engine.setLayerImage(layer, black, &error), error);
            require(engine.addEffect(layer, root + name + ".fs", &error) >= 0, error);
            return layer;
        };
        int layer = setup("Motion Difference Absolute");
        auto *fx = engine.layer(layer)->effects[0].get();
        fx->input("lighting")->lValue = 0;
        engine.renderFrame();
        require(mean(engine.grabOutput()) < .01, "absolute initializes a quiet reference");
        require(engine.setLayerImage(layer, white, &error), error);
        engine.renderFrame();
        require(mean(engine.grabOutput()) > .95, "absolute raw difference detects lighting change");
        fx->input("lighting")->lValue = 1;
        engine.renderFrame();
        require(mean(engine.grabOutput()) < .01, "global illumination compensation ignores uniform brightness change");
        engine.setLayerImage(layer, black, &error);
        fx->input("capture")->eventFired = true;
        engine.renderFrame();
        engine.setLayerImage(layer, patch, &error);
        engine.renderFrame();
        require(std::abs(mean(engine.grabOutput()) - .25) < .03,
                "foreground detected without global false-positive from mean shift");
        fx->input("capture")->eventFired = true;
        engine.renderFrame();
        require(mean(engine.grabOutput()) < .01, "capture reference event");
        engine.setLayerImage(layer, black, &error);
        engine.renderFrame();
        require(std::abs(mean(engine.grabOutput()) - .25) < .03, "foreground departure is detected locally");
        layer = setup("Motion Difference Relative");
        fx = engine.layer(layer)->effects[0].get();
        fx->input("lighting")->lValue = 0;
        engine.renderFrame();
        require(mean(engine.grabOutput()) < .01, "relative initializes quiet history");
        engine.setLayerImage(layer, white, &error);
        engine.renderFrame();
        require(mean(engine.grabOutput()) > .95, "relative detects moving frame");
        engine.renderFrame();
        require(mean(engine.grabOutput()) < .01, "relative ignores unchanged frame");
        fx->input("historyFrames")->lValue = 3;
        engine.setLayerImage(layer, black, &error);
        engine.renderFrame();
        require(mean(engine.grabOutput()) < .01, "three-frame lag reads old black frame");
        engine.renderFrame();
        require(mean(engine.grabOutput()) > .95, "three-frame lag differs from immediate previous frame");
        fx->input("reset")->eventFired = true;
        engine.renderFrame();
        require(mean(engine.grabOutput()) < .01, "history reset suppresses stale motion");
        engine.renderFrame();
        require(mean(engine.grabOutput()) < .01, "history reset fills all previous slots");
        fx->input("historyFrames")->lValue = 1;
        fx->input("persistence")->fValue = .8;
        engine.setLayerImage(layer, white, &error);
        engine.renderFrame();
        engine.renderFrame();
        const double trail = mean(engine.grabOutput());
        require(trail > .6 && trail < .98, "temporal motion persistence");
        fx->input("persistence")->fValue = 0;
        fx->input("historyFrames")->lValue = 2;
        fx->input("historyMode")->lValue = 1;
        fx->input("outputMode")->lValue = 0;
        fx->input("reset")->eventFired = true;
        engine.setLayerImage(layer, black, &error);
        engine.renderFrame();
        engine.setLayerImage(layer, white, &error);
        engine.renderFrame();
        require(mean(engine.grabOutput()) > .95, "averaged history starts with reset black frames");
        engine.renderFrame();
        require(std::abs(mean(engine.grabOutput()) - .474) < .03,
                "average of previous two frames produces a graded difference");
        engine.renderFrame();
        require(mean(engine.grabOutput()) < .01, "averaged history catches up to static scene");
        // Analyze the post-FX mask separately and map it into an ordinary parameter.
        fx->input("persistence")->fValue = 0;
        fx->input("reset")->eventFired = true;
        engine.renderFrame();
        engine.newProject();
        engine.setCompositionSize(QSize(32, 32));
        layer = engine.addLayer("Analyzed image");
        const auto sourceId = engine.layerId(layer);
        require(engine.setLayerImage(layer, white, &error), error);
        const int target = engine.addLayer("Target");
        const auto targetId = engine.layerId(target);
        auto input = Interaction::made(InteractionSource::Pixels);
        input.pixels.layer = sourceId;
        input.pixels.tap = 2;
        input.pixelFeature = PixelAnalysis::Feature::Mean;
        input.layer = targetId;
        input.param = "opacity";
        input.filters.clear();
        const auto interactionId = engine.addInteraction(input);
        require(engine.setParameter(sourceId, "enable", false), "hide analyzed source");
        for (int i = 0; i < 12; ++i) {
            engine.renderFrame();
            QThread::msleep(3);
        }
        require(engine.parameterValue(targetId, "opacity").toDouble() > .95, "async pixel interaction drives target");
        engine.setLayerImage(engine.indexOfId(sourceId), black, &error);
        for (int i = 0; i < 12; ++i) {
            engine.renderFrame();
            QThread::msleep(3);
        }
        require(engine.parameterValue(targetId, "opacity").toDouble() < .01, "pixel interaction follows image changes");
        auto interactions = engine.interactions();
        auto &vision = interactions[0];
        vision.pixels.channel = PixelAnalysis::Channel::Alpha;
        vision.pixels.roi = QRectF(.1, .2, .3, .4);
        vision.pixelFeature = PixelAnalysis::Feature::Energy;
        engine.setInteraction(vision);
        const auto saved = engine.captureInteractions();
        vision.pixels.channel = PixelAnalysis::Channel::Red;
        engine.setInteraction(vision);
        engine.applyInteractions(saved, 0);
        require(engine.interactions()[0].pixels.channel == PixelAnalysis::Channel::Alpha &&
                    engine.interactions()[0].pixelFeature == PixelAnalysis::Feature::Energy &&
                    engine.interactions()[0].pixels.roi == QRectF(.1, .2, .3, .4),
                "vision settings participate in interaction snapshot restore");
        for (int i = 0; i < 12; ++i) {
            engine.renderFrame();
            QThread::msleep(3);
        }
        require(engine.parameterValue(targetId, "opacity").toDouble() > .95,
                "alpha sampler preserves opaque black image alpha");
        const auto project = dir.filePath("vision.nyxmap");
        require(engine.saveProject(project, {}, &error), error);
        require(engine.loadProject(project, nullptr, &error), error);
        require(engine.interactions()[0].id == interactionId && engine.interactions()[0].pixels.layer == sourceId &&
                    engine.interactions()[0].pixelFeature == PixelAnalysis::Feature::Energy,
                "vision project roundtrip");
        engine.shutdown();
        std::cout << "GPU motion masks, lighting compensation, history, async analysis and project restore passed\n";
        return 0;
    } catch (const std::exception &e) {
        engine.shutdown();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
