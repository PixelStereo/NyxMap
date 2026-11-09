#pragma once
#include <QImage>
#include <QJsonObject>
#include <functional>
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
    Count,
    GestureFist, GesturePalm, GesturePoint, GestureThumbUp, GestureThumbDown, GestureVictory, GestureLove,
    FaceCount, FaceX, FaceY, JawOpen, BlinkLeft, BlinkRight,
    PoseCount, NoseX, NoseY, LeftWristX, LeftWristY, RightWristX, RightWristY,
    ObjectCount, ObjectX, ObjectY, ObjectWidth, ObjectHeight, ObjectScore,
    ForegroundCoverage,
    HandPointX, HandPointY, HandPointZ, HandWorldX, HandWorldY, HandWorldZ,
    FacePointX, FacePointY, FacePointZ, FaceBlendshape,
    PosePointX, PosePointY, PosePointZ, PoseWorldX, PoseWorldY, PoseWorldZ, PoseVisibility,
    FaceDetectionCount, FaceBoxX, FaceBoxY, FaceBoxWidth, FaceBoxHeight, FaceDetectionScore,
    ClassificationScore, ClassificationIndex, FaceTransform, HandLeftness, HandRightness
};
QStringList featureNames();
unsigned taskFor(Feature feature);
inline constexpr size_t pictureCount = 9;
inline constexpr std::array<unsigned, pictureCount> pictureTasks{{32, 256, 256, 32, 8, 512, 1024, 2048, 256}};

struct Hand {
    bool valid = false;
    QString gesture;
    float confidence = 0, handedness = 0;
    std::array<std::array<float, 3>, 21> world{};
    std::array<std::array<float, 3>, 21> points{}; // normalized image x/y, relative depth
};
double measure(const Hand &hand, Feature feature);
struct Frame {
    qint64 timeNs = 0, completedNs = 0;
    std::array<Hand, 2> hands{};
    QString error;
    QImage preview;
    std::array<QImage, pictureCount> pictures;
    QJsonObject detections;
};
double measure(const Frame &frame, int index, Feature feature, double confidence, int landmark = 0);
QString taskError(const Frame &frame, Feature feature);

class Detector {
public:
    explicit Detector(std::function<bool()> cancelled = {});
    ~Detector();
    bool load(const QString &directory, QString *error);
    std::vector<Hand> detect(const QImage &image, QString *error);
    QJsonObject infer(const QImage &image, unsigned tasks, QString *error);
    static std::vector<Hand> decodeHands(const QJsonObject &result);

private:
    struct Private;
    std::unique_ptr<Private> d;
};
class Worker {
public:
    explicit Worker(QString directory);
    ~Worker();
    void setDirectory(QString directory);
    void submit(QString source, QImage image, qint64 timeNs, unsigned tasks = 1);
    Frame latest(const QString &source) const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};
} // namespace HandTracking
