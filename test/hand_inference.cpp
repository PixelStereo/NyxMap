#include "HandTracking.h"
#include <QCoreApplication>
#include <QDir>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto directory = qEnvironmentVariable("NYXMAP_HAND_MODELS");
    if (directory.isEmpty()) {
        std::cout << "Set NYXMAP_HAND_MODELS to the MediaPipe Tasks model folder\n";
        return 77;
    }
    HandTracking::Detector detector;
    QString error;
    if (!detector.load(directory, &error)) {
        std::cerr << error.toStdString();
        return 1;
    }
    QImage blank(320, 240, QImage::Format_RGB32);
    blank.fill(Qt::black);
    const auto hands = detector.detect(blank, &error);
    if (!error.isEmpty() || !hands.empty()) {
        std::cerr << "Blank frame inference failed: " << error.toStdString();
        return 1;
    }
    for (const auto &feature : {HandTracking::Feature::GestureFist, HandTracking::Feature::FaceCount,
                                HandTracking::Feature::PoseCount, HandTracking::Feature::ObjectCount,
                                HandTracking::Feature::ForegroundCoverage, HandTracking::Feature::FaceDetectionCount,
                                HandTracking::Feature::ClassificationScore}) {
        if (!qEnvironmentVariableIsSet("NYXMAP_MEDIAPIPE_ALL_MODELS")) break;
        HandTracking::Frame result;
        result.detections = detector.infer(blank, HandTracking::taskFor(feature), &result.error);
        for (const auto &key : result.detections.keys())
            if (key.endsWith("_delegate")) std::cout << key.toStdString() << ": "
                                                       << result.detections.value(key).toString().toStdString() << '\n';
        const auto failure = HandTracking::taskError(result, feature);
        if (!failure.isEmpty() || result.detections.isEmpty()) {
            std::cerr << "MediaPipe task failed: " << failure.toStdString();
            return 1;
        }
    }
    if (qEnvironmentVariableIsSet("NYXMAP_MEDIAPIPE_SEGMENTATION_MODELS")) {
        for (const auto &variant : {std::pair{8, "parts_foreground"}, std::pair{9, "landscape"},
                                    std::pair{10, "hair_detail"}, std::pair{11, "deeplab"}}) {
            const auto result = detector.infer(blank, 1u << variant.first, &error);
            if (!error.isEmpty() || result.value(variant.second).toString().isEmpty()) {
                std::cerr << "Missing segmentation output: " << variant.second << '\n';
                return 1;
            }
        }
    }
    const auto fixture = qEnvironmentVariable("NYXMAP_HAND_IMAGE");
    if (!fixture.isEmpty()) {
        QImage image(fixture);
        if (image.isNull()) {
            std::cerr << "Cannot decode hand fixture: " << fixture.toStdString();
            return 1;
        }
        const auto found = detector.detect(image, &error);
        if (!error.isEmpty() || found.empty()) {
            std::cerr << "No hand in fixture: " << error.toStdString();
            return 1;
        }
        if (qEnvironmentVariableIsSet("NYXMAP_MEDIAPIPE_ALL_MODELS")) {
            const auto result = detector.infer(image, HandTracking::taskFor(HandTracking::Feature::GestureThumbUp), &error);
            const auto gestures = HandTracking::Detector::decodeHands(result);
            if (!error.isEmpty() || gestures.empty() || gestures.front().gesture.isEmpty()) {
                std::cerr << "Gesture classification failed: " << error.toStdString();
                return 1;
            }
            std::cout << "Recognized gesture: " << gestures.front().gesture.toStdString() << '\n';
        }
        std::cout << found.size() << " hand(s) detected in fixture\n";
    }
    std::cout << "Verified models loaded and CPU inference completed\n";
    return 0;
}
