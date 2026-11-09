#include "HandTracking.h"
#include "MediaPipeWorkerScript.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

QStringList HandTracking::featureNames()
{
    return {"Presence",    "Confidence",  "Wrist X",        "Wrist Y",       "Index tip X",   "Index tip Y",
            "Thumb tip X", "Thumb tip Y", "Pinch strength", "Hand openness", "Hand rotation", "Visible hand count",
            "Gesture · Closed fist", "Gesture · Open palm", "Gesture · Point up", "Gesture · Thumb up",
            "Gesture · Thumb down", "Gesture · Victory", "Gesture · I love you",
            "Face · Count", "Face · X", "Face · Y", "Face · Jaw open", "Face · Left blink", "Face · Right blink",
            "Pose · Count", "Pose · Nose X", "Pose · Nose Y", "Pose · Left wrist X", "Pose · Left wrist Y",
            "Pose · Right wrist X", "Pose · Right wrist Y", "Object · Count", "Object · Center X", "Object · Center Y",
            "Object · Width", "Object · Height", "Object · Confidence", "Segmentation · Foreground coverage",
            "Hand · Landmark X", "Hand · Landmark Y", "Hand · Landmark Z", "Hand · World X", "Hand · World Y", "Hand · World Z",
            "Face · Landmark X", "Face · Landmark Y", "Face · Landmark Z", "Face · Blendshape",
            "Pose · Landmark X", "Pose · Landmark Y", "Pose · Landmark Z", "Pose · World X", "Pose · World Y",
            "Pose · World Z", "Pose · Landmark visibility", "Face detection · Count", "Face detection · Center X",
            "Face detection · Center Y", "Face detection · Width", "Face detection · Height", "Face detection · Confidence",
            "Classification · Confidence", "Classification · Class index", "Face · Transform matrix component",
            "Hand · Leftness", "Hand · Rightness"};
}
double HandTracking::measure(const Hand &hand, Feature feature)
{
    if (!hand.valid)
        return 0;
    auto distance = [&](int a, int b) {
        return std::hypot(double(hand.points[a][0] - hand.points[b][0]), double(hand.points[a][1] - hand.points[b][1]));
    };
    const double scale = std::max(.001, distance(0, 9));
    switch (feature) {
    case Feature::Presence:
        return 1;
    case Feature::Confidence:
        return hand.confidence;
    case Feature::WristX:
        return hand.points[0][0];
    case Feature::WristY:
        return hand.points[0][1];
    case Feature::IndexX:
        return hand.points[8][0];
    case Feature::IndexY:
        return hand.points[8][1];
    case Feature::ThumbX:
        return hand.points[4][0];
    case Feature::ThumbY:
        return hand.points[4][1];
    case Feature::Pinch:
        return std::clamp(1 - distance(4, 8) / (scale * .7), 0., 1.);
    case Feature::Openness: {
        double sum = 0;
        for (int base : {5, 9, 13, 17})
            sum += std::clamp((distance(0, base + 3) / std::max(.001, distance(0, base)) - 1.) / 1.1, 0., 1.);
        return sum / 4;
    }
    case Feature::Rotation:
        return (std::atan2(hand.points[9][1] - hand.points[0][1], hand.points[9][0] - hand.points[0][0]) +
                3.141592653589793) /
               (2 * 3.141592653589793);
    case Feature::Count:
        return 1;
    default:
        return 0;
    }
    return 0;
}
unsigned HandTracking::taskFor(Feature feature)
{
    if (feature >= Feature::HandLeftness) return 1;
    if (feature == Feature::FaceTransform) return 4;
    if (feature >= Feature::ClassificationScore) return 128;
    if (feature >= Feature::FaceDetectionCount) return 64;
    if (feature >= Feature::PosePointX) return 8;
    if (feature >= Feature::FacePointX) return 4;
    if (feature >= Feature::HandPointX) return 1;
    if (feature < Feature::GestureFist) return 1;
    if (feature < Feature::FaceCount) return 2;
    if (feature < Feature::PoseCount) return 4;
    if (feature < Feature::ObjectCount) return 8;
    if (feature < Feature::ForegroundCoverage) return 16;
    return 32;
}

QString HandTracking::taskError(const Frame &frame, Feature feature)
{
    if (!frame.error.isEmpty()) return frame.error;
    const auto task = taskFor(feature);
    const char *key = task == 1 ? "hands" : task == 2 ? "gestures" : task == 4 ? "face" :
                      task == 8 ? "pose" : task == 16 ? "objects" : task == 32 ? "segment" :
                      task == 64 ? "face_detector" : "classification";
    return frame.detections.value(QString::fromLatin1(key) + "_error").toString();
}

double HandTracking::measure(const Frame &frame, int index, Feature feature, double confidence, int landmarkIndex)
{
    const auto &hand = frame.hands[size_t(std::clamp(index, 0, 1))];
    if (feature == Feature::HandLeftness || feature == Feature::HandRightness) {
        if (!hand.valid) return 0;
        const double right = hand.handedness > 0.5 ? hand.confidence : 1.0 - hand.confidence;
        return feature == Feature::HandRightness ? right : 1.0 - right;
    }
    if (feature == Feature::FaceTransform) {
        const auto all = frame.detections.value("face").toArray();
        if (index < 0 || index >= all.size() || landmarkIndex < 0) return 0;
        const auto matrix = all.at(index).toObject().value("matrix").toArray();
        return landmarkIndex < matrix.size() ? matrix.at(landmarkIndex).toDouble() : 0;
    }
    if (feature == Feature::ClassificationScore || feature == Feature::ClassificationIndex) {
        const auto all = frame.detections.value("classification").toArray();
        if (index < 0 || index >= all.size()) return 0;
        return all.at(index).toObject().value(feature == Feature::ClassificationScore ? "score" : "index").toDouble();
    }
    if (feature >= Feature::FaceDetectionCount) {
        const auto all = frame.detections.value("face_detector").toArray();
        if (feature == Feature::FaceDetectionCount) return all.size();
        if (index < 0 || index >= all.size()) return 0;
        const auto detection = all.at(index).toObject();
        if (feature == Feature::FaceDetectionScore) return detection.value("score").toDouble();
        const auto box = detection.value("box").toArray();
        if (box.size() != 4) return 0;
        const int component = int(feature) - int(Feature::FaceBoxX);
        return box.at(component).toDouble() + (component < 2 ? box.at(component + 2).toDouble() / 2 : 0);
    }
    if (feature >= Feature::HandPointX) {
        if (landmarkIndex < 0) return 0;
        if (feature < Feature::FacePointX) {
            if (!hand.valid || hand.confidence < confidence || landmarkIndex >= int(hand.points.size())) return 0;
            const bool world = feature >= Feature::HandWorldX;
            const int axis = int(feature) - int(world ? Feature::HandWorldX : Feature::HandPointX);
            return (world ? hand.world : hand.points)[size_t(landmarkIndex)][size_t(axis)];
        }
        const bool face = feature < Feature::PosePointX;
        const auto all = frame.detections.value(face ? "face" : "pose").toArray();
        if (index < 0 || index >= all.size()) return 0;
        const auto detection = all.at(index).toObject();
        if (feature == Feature::FaceBlendshape)
            return detection.value("blendshapes").toObject().value(QString::number(landmarkIndex)).toDouble();
        if (feature == Feature::PoseVisibility) {
            const auto values = detection.value("visibility").toArray();
            return landmarkIndex < values.size() ? values.at(landmarkIndex).toDouble() : 0;
        }
        const bool world = feature >= Feature::PoseWorldX;
        const int axis = int(feature) - int(world ? Feature::PoseWorldX : face ? Feature::FacePointX : Feature::PosePointX);
        const auto points = detection.value(world ? "world" : "points").toArray();
        if (landmarkIndex >= points.size()) return 0;
        const auto point = points.at(landmarkIndex).toArray();
        return axis >= 0 && axis < point.size() ? point.at(axis).toDouble() : 0;
    }
    if (feature == Feature::Count)
        return std::count_if(frame.hands.begin(), frame.hands.end(), [confidence](const auto &h) { return h.valid && h.confidence >= confidence; });
    if (feature < Feature::GestureFist) return hand.confidence >= confidence ? measure(hand, feature) : 0;
    if (feature < Feature::FaceCount) {
        static const char *names[] = {"Closed_Fist", "Open_Palm", "Pointing_Up", "Thumb_Up", "Thumb_Down", "Victory", "ILoveYou"};
        return hand.valid && hand.confidence >= confidence && hand.gesture == QLatin1String(names[int(feature) - int(Feature::GestureFist)]);
    }
    if (feature == Feature::ForegroundCoverage) return frame.detections.value("coverage").toDouble();
    const bool face = feature < Feature::PoseCount, pose = feature >= Feature::PoseCount && feature < Feature::ObjectCount;
    const auto all = frame.detections.value(face ? "face" : pose ? "pose" : "objects").toArray();
    if (feature == Feature::FaceCount || feature == Feature::PoseCount || feature == Feature::ObjectCount) return all.size();
    if (index < 0 || index >= all.size()) return 0;
    const auto item = all.at(index).toObject();
    if (face && feature >= Feature::JawOpen) {
        static const char *keys[] = {"jawOpen", "eyeBlinkLeft", "eyeBlinkRight"};
        return item.value("expressions").toObject().value(keys[int(feature) - int(Feature::JawOpen)]).toDouble();
    }
    if (!face && !pose) {
        const auto box = item.value("box").toArray();
        if (box.size() != 4) return 0;
        switch (feature) {
        case Feature::ObjectX: return box.at(0).toDouble() + box.at(2).toDouble() / 2;
        case Feature::ObjectY: return box.at(1).toDouble() + box.at(3).toDouble() / 2;
        case Feature::ObjectWidth: return box.at(2).toDouble();
        case Feature::ObjectHeight: return box.at(3).toDouble();
        default: return item.value("score").toDouble();
        }
    }
    const int offset = face ? int(feature) - int(Feature::FaceX) : int(feature) - int(Feature::NoseX);
    const auto landmarks = item.value("points").toArray();
    const int landmark = face ? 1 : offset < 2 ? 0 : offset < 4 ? 15 : 16;
    if (landmark >= landmarks.size()) return 0;
    const auto point = landmarks.at(landmark).toArray();
    return point.size() >= 2 ? point.at(offset % 2).toDouble() : 0;
}

namespace {
QString mediaPipePython(const QString &directory = {})
{
    const QString configured = qEnvironmentVariable("NYXMAP_MEDIAPIPE_PYTHON");
    if (!configured.isEmpty()) return configured;
#ifdef Q_OS_WIN
    const QString local = QDir(directory).filePath(".venv/Scripts/python.exe");
    const char *name = "python.exe";
#else
    const QString local = QDir(directory).filePath(".venv/bin/python");
    const char *name = "python3";
#endif
    if (!directory.isEmpty() && QFileInfo(local).isExecutable()) return QFileInfo(local).absoluteFilePath();
    return QStandardPaths::findExecutable(QLatin1String(name));
}
}

struct HandTracking::Detector::Private {
    QProcess process;
    std::function<bool()> cancelled;
    bool cpuAfterCrash = false;
    explicit Private(std::function<bool()> stop) : cancelled(std::move(stop)) {
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("MPLBACKEND"), QStringLiteral("Agg"));
        process.setProcessEnvironment(environment);
        process.setStandardErrorFile(QProcess::nullDevice());
    }
    ~Private() { close(); }
    void close() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(500);
        }
    }
    QJsonObject response(int timeout, QString *error) {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!process.canReadLine() && process.state() != QProcess::NotRunning &&
               elapsed.elapsed() < timeout && !(cancelled && cancelled()))
            process.waitForReadyRead(50);
        if (!process.canReadLine()) {
            const auto mode = qEnvironmentVariable("NYXMAP_MEDIAPIPE_DELEGATE", "auto").toLower();
            if (mode == "auto" && process.state() == QProcess::NotRunning && process.exitStatus() == QProcess::CrashExit)
                cpuAfterCrash = true;
            if (error) *error = "MediaPipe worker stopped or timed out";
            close();
            return {};
        }
        const auto result = QJsonDocument::fromJson(process.readLine()).object();
        if (error) *error = result.isEmpty() ? QStringLiteral("Invalid MediaPipe worker reply") : result.value("error").toString();
        return result;
    }
};
HandTracking::Detector::Detector(std::function<bool()> cancelled) : d(std::make_unique<Private>(std::move(cancelled))) {}
HandTracking::Detector::~Detector() = default;
bool HandTracking::Detector::load(const QString &directory, QString *error)
{
    d->close();
    if (!QDir(directory).exists() || QDir(directory).entryList({"*.task", "*.tflite"}, QDir::Files).isEmpty()) {
        if (error) *error = "Choose a MediaPipe model folder. See docs/vision-interactions.md.";
        return false;
    }
    const QString python = mediaPipePython(directory);
    if (python.isEmpty()) {
        if (error) *error = "Python with MediaPipe Tasks is required. Set NYXMAP_MEDIAPIPE_PYTHON.";
        return false;
    }
    if (d->cpuAfterCrash) {
        auto environment = d->process.processEnvironment();
        environment.insert(QStringLiteral("NYXMAP_MEDIAPIPE_DELEGATE"), QStringLiteral("cpu"));
        d->process.setProcessEnvironment(environment);
    }
    d->process.start(python, {"-u", "-c", QString::fromUtf8(mediaPipeWorkerScript), directory});
    if (!d->process.waitForStarted(500)) {
        if (error) *error = d->process.errorString();
        return false;
    }
    const auto reply = d->response(30000, error);
    if (reply.value("ready").toBool()) return true;
    d->close();
    return false;
}
QJsonObject HandTracking::Detector::infer(const QImage &image, unsigned tasks, QString *error)
{
    if (image.isNull() || d->process.state() != QProcess::Running) {
        if (error) *error = "MediaPipe worker is unavailable";
        return {};
    }
    const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
    const QByteArray header = QByteArray::number(rgba.width()) + ' ' + QByteArray::number(rgba.height()) + ' ' + QByteArray::number(tasks) + char(10);
    d->process.write(header);
    d->process.write(reinterpret_cast<const char *>(rgba.constBits()), rgba.sizeInBytes());
    return d->response(2500, error);
}

std::vector<HandTracking::Hand> HandTracking::Detector::decodeHands(const QJsonObject &result)
{
    std::vector<Hand> hands;
    for (const auto &entry : result.value("hands").toArray()) {
        const auto item = entry.toObject();
        const auto points = item.value("points").toArray();
        const auto world = item.value("world").toArray();
        if (points.size() != 21) continue;
        Hand hand;
        hand.valid = true;
        hand.confidence = float(item.value("score").toDouble());
        hand.handedness = item.value("right").toBool() ? 1.f : 0.f;
        hand.gesture = item.value("gesture").toString();
        for (int i = 0; i < 21; ++i) {
            const auto point = points.at(i).toArray();
            if (point.size() != 3) { hand.valid = false; break; }
            const auto worldPoint = i < world.size() ? world.at(i).toArray() : QJsonArray{};
            for (int j = 0; j < 3; ++j) {
                hand.points[i][j] = float(point.at(j).toDouble());
                if (j < worldPoint.size()) hand.world[i][j] = float(worldPoint.at(j).toDouble());
            }
        }
        if (hand.valid) hands.push_back(hand);
    }
    return hands;
}
std::vector<HandTracking::Hand> HandTracking::Detector::detect(const QImage &image, QString *error)
{
    const auto result = infer(image, 1, error);
    if (error && result.contains("hands_error")) *error = result.value("hands_error").toString();
    return decodeHands(result);
}

struct HandTracking::Worker::Private {
    struct Job {
        QString source;
        QImage image;
        qint64 timeNs;
        uint64_t generation;
        unsigned tasks;
    };
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::map<QString, Frame> frames;
    QString directory;
    uint64_t generation = 0;
    std::atomic<bool> stop{false};
    std::thread thread;
    explicit Private(QString path) : directory(std::move(path)), thread([this] { run(); }) {}
    ~Private()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        wake.notify_all();
        thread.join();
    }
    void run()
    {
        Detector detector([this] { return stop.load(); });
        QString loaded;
        bool ready = false;
        uint64_t loadedGeneration = uint64_t(-1);
        auto retry = std::chrono::steady_clock::now();
        while (true) {
            Job job;
            QString path;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stop || !jobs.empty(); });
                if (stop)
                    return;
                job = std::move(jobs.front());
                jobs.pop_front();
                path = directory;
            }
            Frame frame;
            frame.timeNs = job.timeNs;
            frame.preview = job.image;
            const auto now = std::chrono::steady_clock::now();
            if (loadedGeneration != job.generation || (!ready && now >= retry)) {
                ready = detector.load(path, &loaded);
                loadedGeneration = job.generation;
                retry = now + std::chrono::seconds(2);
            }
            if (ready) {
                frame.detections = detector.infer(job.image, job.tasks, &frame.error);
                if (!frame.error.isEmpty()) { ready = false; loaded = frame.error; }
                auto found = Detector::decodeHands(frame.detections);
                Frame previous;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    auto p = frames.find(job.source);
                    if (p != frames.end() && job.timeNs - p->second.timeNs < 500000000)
                        previous = p->second;
                }
                // Keep two wrist tracks stable instead of sorting by detector confidence every frame.
                std::array<bool, 2> used{};
                for (const auto &hand : found) {
                    int slot = -1;
                    double distance = 1e9;
                    for (int i = 0; i < 2; ++i)
                        if (!used[i] && previous.hands[i].valid) {
                            double d = std::hypot(hand.points[0][0] - previous.hands[i].points[0][0],
                                                  hand.points[0][1] - previous.hands[i].points[0][1]);
                            if (d < distance) {
                                distance = d;
                                slot = i;
                            }
                        }
                    if (slot < 0)
                        for (int i = 0; i < 2; ++i)
                            if (!used[i]) {
                                slot = i;
                                break;
                            }
                    if (slot >= 0) {
                        frame.hands[slot] = hand;
                        used[slot] = true;
                    }
                }
                QPainter painter(&frame.preview);
                const int maskWidth = frame.detections.value("mask_width").toInt();
                const int maskHeight = frame.detections.value("mask_height").toInt();
                const auto mask = QByteArray::fromBase64(frame.detections.take("mask").toString().toLatin1());
                if (maskWidth > 0 && maskHeight > 0 && mask.size() == qint64(maskWidth) * maskHeight) {
                    QImage overlay(maskWidth, maskHeight, QImage::Format_ARGB32_Premultiplied);
                    frame.pictures[0] = QImage(maskWidth, maskHeight, QImage::Format_RGBA8888);
                    for (int y = 0; y < maskHeight; ++y) {
                        auto *row = reinterpret_cast<QRgb *>(overlay.scanLine(y));
                        auto *alpha = frame.pictures[0].scanLine(y);
                        for (int x = 0; x < maskWidth; ++x) {
                            const int opacity = uchar(mask.at(qsizetype(y) * maskWidth + x));
                            row[x] = qPremultiply(qRgba(30, 180, 255, opacity * 105 / 255));
                            std::fill_n(alpha + x * 4, 3, uchar(255));
                            alpha[x * 4 + 3] = uchar(opacity);
                        }
                    }
                    // Apply the confidence map to the exact input frame, even if the source has moved on.
                    auto cutout = job.image.convertToFormat(QImage::Format_RGBA8888);
                    const auto scaled = frame.pictures[0].scaled(cutout.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    for (int y = 0; y < cutout.height(); ++y) {
                        auto *row = cutout.scanLine(y);
                        const auto *alpha = scaled.constScanLine(y);
                        for (int x = 0; x < cutout.width(); ++x)
                            row[x * 4 + 3] = uchar((int(row[x * 4 + 3]) * int(alpha[x * 4 + 3]) + 127) / 255);
                    }
                    frame.pictures[3] = std::move(cutout);
                    painter.drawImage(frame.preview.rect(), overlay);
                }
                const int partsWidth = frame.detections.value("parts_width").toInt();
                const int partsHeight = frame.detections.value("parts_height").toInt();
                const auto parts = QByteArray::fromBase64(frame.detections.take("parts").toString().toLatin1());
                if (partsWidth > 0 && partsHeight > 0 && parts.size() == qint64(partsWidth) * partsHeight) {
                    static const QRgb palette[] = {0, qRgb(242, 96, 220), qRgb(255, 181, 90),
                                                   qRgb(100, 208, 240), qRgb(67, 221, 138), qRgb(254, 222, 80)};
                    auto &hair = frame.pictures[1];
                    auto &colors = frame.pictures[2];
                    hair = QImage(partsWidth, partsHeight, QImage::Format_ARGB32);
                    colors = QImage(partsWidth, partsHeight, QImage::Format_ARGB32);
                    for (int y = 0; y < partsHeight; ++y) {
                        auto *hairRow = reinterpret_cast<QRgb *>(hair.scanLine(y));
                        auto *colorRow = reinterpret_cast<QRgb *>(colors.scanLine(y));
                        for (int x = 0; x < partsWidth; ++x) {
                            const auto label = uchar(parts.at(qsizetype(y) * partsWidth + x));
                            hairRow[x] = label == 1 ? qRgb(255, 255, 255) : 0;
                            colorRow[x] = label < std::size(palette) ? palette[label] : 0;
                        }
                    }
                }
                static const char *alphaOutputs[] = {"landscape", "hair_detail", "deeplab", "parts_foreground"};
                for (size_t i = 0; i < std::size(alphaOutputs); ++i) {
                    const auto key = QString::fromLatin1(alphaOutputs[i]);
                    const int width = frame.detections.value(key + "_width").toInt();
                    const int height = frame.detections.value(key + "_height").toInt();
                    const auto data = QByteArray::fromBase64(frame.detections.take(key).toString().toLatin1());
                    if (width <= 0 || height <= 0 || data.size() != qint64(width) * height) continue;
                    auto &image = frame.pictures[i + 5];
                    image = QImage(width, height, QImage::Format_RGBA8888);
                    for (int y = 0; y < height; ++y) {
                        auto *line = image.scanLine(y);
                        for (int x = 0; x < width; ++x) {
                            std::fill_n(line + x * 4, 3, uchar(255));
                            line[x * 4 + 3] = uchar(data.at(qsizetype(y) * width + x));
                        }
                    }
                }
                if (frame.detections.contains("pose")) {
                    auto &skeleton = frame.pictures[4];
                    skeleton = QImage(job.image.size(), QImage::Format_ARGB32_Premultiplied);
                    skeleton.fill(Qt::transparent);
                    QPainter posePainter(&skeleton);
                    posePainter.setRenderHint(QPainter::Antialiasing);
                    // Map MediaPipe's body to the OpenPose COCO-18 joint order. Neck is the shoulder midpoint.
                    static constexpr int joints[] = {0, -1, 12, 14, 16, 11, 13, 15, 24, 26, 28, 23, 25, 27, 5, 2, 8, 7};
                    static constexpr int links[][2] = {{1,2}, {1,5}, {2,3}, {3,4}, {5,6}, {6,7}, {1,8}, {8,9}, {9,10},
                                                      {1,11}, {11,12}, {12,13}, {1,0}, {0,14}, {14,16}, {0,15}, {15,17}};
                    for (const auto &person : frame.detections.value("pose").toArray()) {
                        const auto data = person.toObject();
                        const auto points = data.value("points").toArray();
                        const auto visibility = data.value("visibility").toArray();
                        if (points.size() < 33 || visibility.size() < 33) continue;
                        const auto point = [&](int index) {
                            const auto xy = points.at(index).toArray();
                            return QPointF(xy.at(0).toDouble() * skeleton.width(), xy.at(1).toDouble() * skeleton.height());
                        };
                        std::array<QPointF, std::size(joints)> mapped;
                        std::array<bool, std::size(joints)> visible{};
                        for (size_t joint = 0; joint < mapped.size(); ++joint) {
                            const int index = joints[joint];
                            mapped[joint] = index >= 0 ? point(index) : (point(11) + point(12)) / 2;
                            visible[joint] = index >= 0 ? visibility.at(index).toDouble() >= 0.5
                                                        : std::min(visibility.at(11).toDouble(), visibility.at(12).toDouble()) >= 0.5;
                        }
                        const double radius = std::max(1., std::min(skeleton.width(), skeleton.height()) * .009);
                        for (size_t link = 0; link < std::size(links); ++link) {
                            const int from = links[link][0], to = links[link][1];
                            if (!visible[from] || !visible[to]) continue;
                            posePainter.setPen(QPen(QColor::fromHsv(int(link) * 20, 255, 235), radius * 2, Qt::SolidLine, Qt::RoundCap));
                            posePainter.drawLine(mapped[from], mapped[to]);
                        }
                        posePainter.setPen(Qt::NoPen);
                        for (size_t joint = 0; joint < mapped.size(); ++joint)
                            if (visible[joint]) {
                                posePainter.setBrush(QColor::fromHsv(int(joint) * 20, 255, 255));
                                posePainter.drawEllipse(mapped[joint], radius, radius);
                            }
                    }
                }
                const auto screenPoint = [&](double x, double y) {
                    return QPointF(x * frame.preview.width(), y * frame.preview.height());
                };
                for (const char *key : {"face", "pose"}) {
                    painter.setPen(QPen(key[0] == 'f' ? QColor(255, 200, 60) : QColor(70, 195, 255), 1));
                    for (const auto &detection : frame.detections.value(key).toArray())
                        for (const auto &landmark : detection.toObject().value("points").toArray()) {
                            const auto point = landmark.toArray();
                            if (point.size() >= 2)
                                painter.drawEllipse(screenPoint(point.at(0).toDouble(), point.at(1).toDouble()), 1, 1);
                        }
                }
                painter.setPen(QPen(QColor(255, 160, 60), 1));
                auto boxes = frame.detections.value("objects").toArray();
                for (const auto &face : frame.detections.value("face_detector").toArray()) boxes.append(face);
                for (const auto &detection : boxes) {
                    const auto item = detection.toObject();
                    const auto box = item.value("box").toArray();
                    if (box.size() != 4) continue;
                    const auto at = screenPoint(box.at(0).toDouble(), box.at(1).toDouble());
                    const auto size = screenPoint(box.at(2).toDouble(), box.at(3).toDouble());
                    painter.drawRect(QRectF(at, QSizeF(size.x(), size.y())));
                    painter.drawText(at + QPointF(2, 12), item.value("label").toString());
                }
                painter.setPen(QPen(QColor(40, 255, 100), 2));
                const auto labels = frame.detections.value("classification").toArray();
                if (!labels.isEmpty()) {
                    const auto top = labels.at(0).toObject();
                    painter.drawText(QPointF(8, 16), top.value("label").toString());
                }
                for (const auto &hand : frame.hands)
                    if (hand.valid) {
                        auto point = [&](int i) {
                            return QPointF(hand.points[i][0] * frame.preview.width(),
                                           hand.points[i][1] * frame.preview.height());
                        };
                        for (int base : {1, 5, 9, 13, 17}) {
                            painter.drawLine(point(0), point(base));
                            for (int i = base; i < base + 3; ++i)
                                painter.drawLine(point(i), point(i + 1));
                        }
                        for (int i = 0; i < 21; ++i)
                            painter.drawEllipse(point(i), 2, 2);
                    }
            } else
                frame.error = loaded;
            {
                std::lock_guard<std::mutex> lock(mutex);
                frame.completedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                if (job.generation == generation) {
                    frames[job.source] = std::move(frame);
                    while (frames.size() > 8) {
                        auto oldest = std::min_element(frames.begin(), frames.end(), [](const auto &a, const auto &b) {
                            return a.second.timeNs < b.second.timeNs;
                        });
                        frames.erase(oldest);
                    }
                }
            }
        }
    }
};
HandTracking::Worker::Worker(QString directory) : d(std::make_unique<Private>(std::move(directory))) {}
HandTracking::Worker::~Worker() = default;
void HandTracking::Worker::setDirectory(QString path)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    d->directory = std::move(path);
    ++d->generation;
    d->jobs.clear();
    d->frames.clear();
}
void HandTracking::Worker::submit(QString source, QImage image, qint64 timeNs, unsigned tasks)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    for (auto &job : d->jobs)
        if (job.source == source) {
            job = {std::move(source), std::move(image), timeNs, d->generation, tasks};
            d->wake.notify_one();
            return;
        }
    if (d->jobs.size() < 8)
        d->jobs.push_back({std::move(source), std::move(image), timeNs, d->generation, tasks});
    d->wake.notify_one();
}
HandTracking::Frame HandTracking::Worker::latest(const QString &source) const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    auto found = d->frames.find(source);
    return found == d->frames.end() ? Frame{} : found->second;
}
