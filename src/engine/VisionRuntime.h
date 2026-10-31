#pragma once
#include "HandTracking.h"
#include "PixelAnalysis.h"
#include <functional>
#include <memory>
#include <vector>

class VisionRuntime {
public:
    struct Request {
        PixelAnalysis::Settings settings;
        bool hands = false;
    };
    struct Texture {
        unsigned int id = 0;
        int width = 0, height = 0;
    };
    VisionRuntime(std::function<void()> drawQuad, QString modelsDirectory);
    ~VisionRuntime();
    void step(const std::vector<Request> &requests,
              const std::function<Texture(const PixelAnalysis::Settings &)> &texture, qint64 timeNs);
    double pixelValue(const PixelAnalysis::Settings &settings, PixelAnalysis::Feature feature, qint64 nowNs);
    HandTracking::Frame hands(const PixelAnalysis::Settings &settings) const;
    QImage preview(const PixelAnalysis::Settings &settings) const;
    QString status(const PixelAnalysis::Settings &settings) const;
    void setModelsDirectory(QString directory);
    void release(); // engine GL context current
private:
    struct Private;
    std::unique_ptr<Private> d;
};
