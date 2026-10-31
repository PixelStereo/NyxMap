// Hand model preprocessing/postprocessing adapted from OpenCV Zoo (Apache-2.0).
// Copyright 2022 The OpenCV Authors. See licenses/opencv-zoo-hands.txt and docs/vision-interactions.md.
#include "HandTracking.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#ifdef NYXMAP_OPENCV_HANDS
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#endif

QStringList HandTracking::featureNames()
{
    return {"Presence",    "Confidence",  "Wrist X",        "Wrist Y",       "Index tip X",   "Index tip Y",
            "Thumb tip X", "Thumb tip Y", "Pinch strength", "Hand openness", "Hand rotation", "Visible hand count"};
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
    }
    return 0;
}
struct HandTracking::Detector::Private {
#ifdef NYXMAP_OPENCV_HANDS
    cv::dnn::Net palm, pose;
    std::vector<cv::Point2f> anchors;
    struct Palm {
        cv::Rect2f box;
        std::array<cv::Point2f, 7> points;
    };
    static cv::Mat blob(const cv::Mat &image, int side, bool bgr = true)
    {
        cv::Mat rgb, resized, floats;
        if (bgr)
            cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
        else
            rgb = image;
        cv::resize(rgb, resized, cv::Size(side, side));
        resized.convertTo(floats, CV_32FC3, 1. / 255);
        int dimensions[] = {1, side, side, 3};
        return cv::Mat(4, dimensions, CV_32F, floats.data).clone();
    }
    std::vector<Palm> palms(const cv::Mat &image)
    {
        const float ratio = 192.f / std::max(image.cols, image.rows);
        cv::Mat resized, padded;
        cv::resize(image, resized,
                   cv::Size(std::max(1, int(image.cols * ratio)), std::max(1, int(image.rows * ratio))));
        const int left = (192 - resized.cols) / 2, top = (192 - resized.rows) / 2;
        cv::copyMakeBorder(resized, padded, top, 192 - resized.rows - top, left, 192 - resized.cols - left,
                           cv::BORDER_CONSTANT);
        palm.setInput(blob(padded, 192));
        std::vector<cv::Mat> output;
        palm.forward(output, palm.getUnconnectedOutLayersNames());
        cv::Mat boxes, scores;
        for (auto &o : output) {
            if (o.total() == 2016 * 18)
                boxes = o.reshape(1, 2016);
            else if (o.total() == 2016)
                scores = o.reshape(1, 2016);
        }
        if (boxes.empty() || scores.empty())
            throw std::runtime_error("Unexpected palm model outputs");
        std::vector<cv::Rect> rectangles;
        std::vector<float> confidence;
        std::vector<Palm> candidates;
        const float scale = float(std::max(image.cols, image.rows));
        const cv::Point2f padding(left / ratio, top / ratio);
        for (int i = 0; i < 2016; ++i) {
            const float score = 1.f / (1.f + std::exp(-std::clamp(scores.at<float>(i), -80.f, 80.f)));
            if (!std::isfinite(score) || score < .5f)
                continue;
            const auto *v = boxes.ptr<float>(i);
            if (!std::all_of(v, v + 18, [](float x) { return std::isfinite(x) && std::abs(x) < 100000; }))
                continue;
            const auto a = anchors[size_t(i)];
            const cv::Point2f center((v[0] / 192 + a.x) * scale - padding.x, (v[1] / 192 + a.y) * scale - padding.y);
            const cv::Size2f size(v[2] * scale / 192, v[3] * scale / 192);
            if (size.width < 2 || size.height < 2)
                continue;
            Palm p;
            p.box = {center.x - size.width / 2, center.y - size.height / 2, size.width, size.height};
            for (int j = 0; j < 7; ++j)
                p.points[j] = {(v[4 + j * 2] / 192 + a.x) * scale - padding.x,
                               (v[5 + j * 2] / 192 + a.y) * scale - padding.y};
            candidates.push_back(p);
            rectangles.emplace_back(int(p.box.x), int(p.box.y), std::max(1, int(p.box.width)),
                                    std::max(1, int(p.box.height)));
            confidence.push_back(score);
        }
        std::vector<int> indices;
        cv::dnn::NMSBoxes(rectangles, confidence, .5f, .3f, indices, 1, 4);
        std::vector<Palm> found;
        for (int i : indices)
            found.push_back(candidates[size_t(i)]);
        return found;
    }
    struct Crop {
        cv::Mat image;
        cv::Rect2f box;
        cv::Point2f bias;
    };
    static Crop crop(const cv::Mat &image, cv::Rect2f box, bool rotate)
    {
        if (!rotate)
            box.y -= box.height * .4f;
        const float factor = rotate ? 4.f : 3.f;
        const auto center = cv::Point2f(box.x + box.width / 2, box.y + box.height / 2);
        const int x1 = std::clamp(int(center.x - box.width * factor / 2), 0, image.cols),
                  y1 = std::clamp(int(center.y - box.height * factor / 2), 0, image.rows);
        const int x2 = std::clamp(int(center.x + box.width * factor / 2), 0, image.cols),
                  y2 = std::clamp(int(center.y + box.height * factor / 2), 0, image.rows);
        if (x2 <= x1 || y2 <= y1)
            return {};
        cv::Mat part = image(cv::Rect(x1, y1, x2 - x1, y2 - y1));
        const int side = rotate ? int(std::hypot(part.cols, part.rows)) : std::max(part.cols, part.rows);
        const int left = (side - part.cols) / 2, top = (side - part.rows) / 2;
        Crop out;
        cv::copyMakeBorder(part, out.image, top, side - part.rows - top, left, side - part.cols - left,
                           cv::BORDER_CONSTANT);
        out.box = {float(x1), float(y1), float(x2 - x1), float(y2 - y1)};
        out.bias = {float(x1 - left), float(y1 - top)};
        return out;
    }
    static cv::Point2f transform(const cv::Mat &matrix, cv::Point2f p)
    {
        return {float(matrix.at<double>(0, 0) * p.x + matrix.at<double>(0, 1) * p.y + matrix.at<double>(0, 2)),
                float(matrix.at<double>(1, 0) * p.x + matrix.at<double>(1, 1) * p.y + matrix.at<double>(1, 2))};
    }
    Hand hand(const cv::Mat &image, const Palm &p)
    {
        // Pre/postprocessing follows OpenCV Zoo's Apache-2.0 MPHandPose reference.
        auto first = crop(image, p.box, true);
        if (first.image.empty())
            return {};
        auto a = p.points[0] - first.bias, b = p.points[2] - first.bias;
        double angle = 3.141592653589793 / 2 - std::atan2(-(b.y - a.y), b.x - a.x);
        angle -= 2 * 3.141592653589793 * std::floor((angle + 3.141592653589793) / (2 * 3.141592653589793));
        angle *= 180 / 3.141592653589793;
        const cv::Point2f center(first.box.x + first.box.width / 2 - first.bias.x,
                                 first.box.y + first.box.height / 2 - first.bias.y);
        const auto rotation = cv::getRotationMatrix2D(center, angle, 1);
        cv::Mat rotated;
        cv::warpAffine(first.image, rotated, rotation, first.image.size());
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (auto point : p.points) {
            const auto r = transform(rotation, point - first.bias);
            minX = std::min(minX, r.x);
            maxX = std::max(maxX, r.x);
            minY = std::min(minY, r.y);
            maxY = std::max(maxY, r.y);
        }
        const auto second = crop(rotated, {minX, minY, maxX - minX, maxY - minY}, false);
        if (second.image.empty())
            return {};
        pose.setInput(blob(second.image, 224));
        std::vector<cv::Mat> out;
        pose.forward(out, pose.getUnconnectedOutLayersNames());
        if (out.size() != 4 || out[0].total() != 63 || out[1].total() != 1 || out[2].total() != 1)
            throw std::runtime_error("Unexpected hand landmark outputs");
        Hand result;
        result.confidence = out[1].ptr<float>()[0];
        result.handedness = out[2].ptr<float>()[0];
        if (!std::isfinite(result.confidence) || result.confidence < .5)
            return result;
        const double scale = std::max(second.box.width, second.box.height) / 224;
        cv::Mat inverse;
        cv::invertAffineTransform(rotation, inverse);
        const auto originalCenter =
            transform(inverse, {second.box.x + second.box.width / 2, second.box.y + second.box.height / 2}) +
            first.bias;
        const double radians = angle * 3.141592653589793 / 180, cos = std::cos(radians), sin = std::sin(radians);
        const float *values = out[0].ptr<float>();
        for (int i = 0; i < 21; ++i) {
            const double x = (values[i * 3] - 112) * scale, y = (values[i * 3 + 1] - 112) * scale;
            const double nx = (x * cos - y * sin + originalCenter.x) / image.cols,
                         ny = (x * sin + y * cos + originalCenter.y) / image.rows;
            if (!std::isfinite(nx) || !std::isfinite(ny))
                return {};
            result.points[i] = {float(std::clamp(nx, 0., 1.)), float(std::clamp(ny, 0., 1.)),
                                float(values[i * 3 + 2] * scale / std::max(image.cols, image.rows))};
        }
        result.valid = true;
        return result;
    }
#endif
};
HandTracking::Detector::Detector() : d(std::make_unique<Private>()) {}
HandTracking::Detector::~Detector() = default;
bool HandTracking::Detector::available()
{
#ifdef NYXMAP_OPENCV_HANDS
    return true;
#else
    return false;
#endif
}
bool HandTracking::Detector::load(const QString &directory, QString *error)
{
#ifdef NYXMAP_OPENCV_HANDS
    const std::array<std::pair<const char *, const char *>, 2> models = {
        {{"palm_detection_mediapipe_2023feb.onnx", "78ff51c38496b7fc8b8ebdb6cc8c1abb02fa6c38427c6848254cdaba57fcce7c"},
         {"handpose_estimation_mediapipe_2023feb.onnx",
          "db0898ae717b76b075d9bf563af315b29562e11f8df5027a1ef07b02bef6d81c"}}};
    for (const auto &model : models) {
        QFile file(QDir(directory).filePath(QLatin1String(model.first)));
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = "Missing hand models: select their directory or run scripts/download_hand_models.py";
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&file);
        if (hash.result().toHex() != model.second) {
            if (error)
                *error = "Hand model checksum mismatch: " + QString::fromUtf8(model.first);
            return false;
        }
    }
    try {
        d->palm = cv::dnn::readNet(QDir(directory).filePath(QLatin1String(models[0].first)).toStdString());
        d->pose = cv::dnn::readNet(QDir(directory).filePath(QLatin1String(models[1].first)).toStdString());
        for (auto *net : {&d->palm, &d->pose}) {
            net->setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net->setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        }
        d->anchors.clear();
        for (const auto size : {24, 12})
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    for (int n = 0; n < (size == 24 ? 2 : 6); ++n)
                        d->anchors.emplace_back((x + .5f) / size, (y + .5f) / size);
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
#else
    Q_UNUSED(directory);
    if (error)
        *error = "Hand tracking requires an OpenCV DNN build";
    return false;
#endif
}
std::vector<HandTracking::Hand> HandTracking::Detector::detect(const QImage &frame, QString *error)
{
#ifdef NYXMAP_OPENCV_HANDS
    if (d->palm.empty() || d->pose.empty())
        return {};
    try {
        const auto image = frame.convertToFormat(QImage::Format_RGBA8888);
        cv::Mat rgba(image.height(), image.width(), CV_8UC4, const_cast<uchar *>(image.constBits()),
                     size_t(image.bytesPerLine())),
            bgr;
        cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
        std::vector<Hand> hands;
        for (const auto &palm : d->palms(bgr)) {
            auto hand = d->hand(bgr, palm);
            if (hand.valid)
                hands.push_back(hand);
            if (hands.size() >= 2)
                break;
        }
        return hands;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return {};
    }
#else
    Q_UNUSED(frame);
    if (error)
        *error = "Hand tracking requires OpenCV DNN";
    return {};
#endif
}
struct HandTracking::Worker::Private {
    struct Job {
        QString source;
        QImage image;
        qint64 timeNs;
        uint64_t generation;
    };
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::map<QString, Frame> frames;
    QString directory;
    uint64_t generation = 0;
    bool stop = false;
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
        Detector detector;
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
                auto found = detector.detect(job.image, &frame.error);
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
                painter.setPen(QPen(QColor(40, 255, 100), 2));
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
void HandTracking::Worker::submit(QString source, QImage image, qint64 timeNs)
{
    std::lock_guard<std::mutex> lock(d->mutex);
    for (auto &job : d->jobs)
        if (job.source == source) {
            job = {std::move(source), std::move(image), timeNs, d->generation};
            d->wake.notify_one();
            return;
        }
    if (d->jobs.size() < 8)
        d->jobs.push_back({std::move(source), std::move(image), timeNs, d->generation});
    d->wake.notify_one();
}
HandTracking::Frame HandTracking::Worker::latest(const QString &source) const
{
    std::lock_guard<std::mutex> lock(d->mutex);
    auto found = d->frames.find(source);
    return found == d->frames.end() ? Frame{} : found->second;
}
