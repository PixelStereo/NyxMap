#pragma once
#include <QImage>
#include <QStringList>
#include <array>
#include <memory>
#include <vector>

namespace HandTracking {
enum class Feature {
    Presence,
    Confidence,
    WristX,
    WristY,
    IndexX,
    IndexY,
    ThumbX,
    ThumbY,
    Pinch,
    Openness,
    Rotation,
    Count
};
QStringList featureNames();
struct Hand {
    bool valid = false;
    float confidence = 0, handedness = 0;
    std::array<std::array<float, 3>, 21> points{}; // normalized image x/y, relative depth
};
double measure(const Hand &hand, Feature feature);
struct Frame {
    qint64 timeNs = 0;
    std::array<Hand, 2> hands{};
    QString error;
    QImage preview;
};
class Detector {
public:
    Detector();
    ~Detector();
    static bool available();
    bool load(const QString &directory, QString *error);
    std::vector<Hand> detect(const QImage &image, QString *error);

private:
    struct Private;
    std::unique_ptr<Private> d;
};
class Worker {
public:
    explicit Worker(QString directory);
    ~Worker();
    void setDirectory(QString directory);
    void submit(QString source, QImage image, qint64 timeNs);
    Frame latest(const QString &source) const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};
} // namespace HandTracking
