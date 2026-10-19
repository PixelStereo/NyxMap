#include "Gl.h"
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir files;
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) {
            ++failures;
            std::printf("FAIL image_import: %s\n", why);
        }
    };
    QImage image(1024, 2, QImage::Format_RGBA64);
    for (int y = 0; y < 2; ++y) {
        auto *row = reinterpret_cast<QRgba64 *>(image.scanLine(y));
        for (int x = 0; x < 1024; ++x)
            row[x] = QRgba64::fromRgba64(quint16(x * 63 + y), quint16(65535 - x * 41), quint16(300 + x),
                                         quint16(20000 + x * 31));
    }
    const QString path = files.path() + "/ramp16.png";
    check(image.save(path), "write a real 16-bit PNG");
    const QImage decoded(path);
    check(decoded.format() == QImage::Format_RGBA64, "PNG decoder retains 16-bit channels");
    const QImage pending = prepareImageUpload(decoded);
    check(pending.format() == QImage::Format_RGBA64, "layer import retains 16-bit precision before upload");
    if (pending.format() == QImage::Format_RGBA64) {
        for (int y = 0; y < 2; ++y) {
            const auto *actual = reinterpret_cast<const QRgba64 *>(pending.constScanLine(y));
            const auto *expected = reinterpret_cast<const QRgba64 *>(image.constScanLine(1 - y));
            for (int x = 0; x < 1024; ++x)
                check(actual[x] == expected[x], "exact RGBA16 samples and vertical orientation retained");
        }
    }
    QImage eight(7, 3, QImage::Format_RGBA8888);
    eight.fill(QColor(12, 34, 56, 78));
    const QString eightPath = files.path() + "/rgba8.png";
    check(eight.save(eightPath), "write an 8-bit PNG");
    check(prepareImageUpload(QImage(eightPath)).format() == QImage::Format_RGBA8888,
          "8-bit sources keep their light upload format");
    QImage gray(6, 1, QImage::Format_Grayscale16);
    const quint16 levels[] = {0, 1, 257, 511, 32768, 65535};
    std::copy(levels, levels + 6, reinterpret_cast<quint16 *>(gray.bits()));
    const QImage grayUpload = prepareImageUpload(gray);
    check(grayUpload.format() == QImage::Format_RGBA64, "16-bit grayscale is not reduced to 8 bits");
    const auto *grayPixels = reinterpret_cast<const QRgba64 *>(grayUpload.constBits());
    for (int x = 0; x < 6; ++x)
        check(grayPixels[x].red() == levels[x] && grayPixels[x].green() == levels[x] && grayPixels[x].alpha() == 65535,
              "grayscale low bits and opaque alpha survive conversion");
    QImage premult(1, 1, QImage::Format_RGBA64_Premultiplied);
    reinterpret_cast<QRgba64 *>(premult.bits())[0] = QRgba64::fromRgba64(4096, 8192, 16384, 32768);
    const QImage straight = prepareImageUpload(premult);
    const auto pixel = reinterpret_cast<const QRgba64 *>(straight.constBits())[0];
    check(straight.format() == QImage::Format_RGBA64 && std::abs(int(pixel.red()) - 8192) <= 1 &&
              std::abs(int(pixel.green()) - 16384) <= 1 && std::abs(int(pixel.blue()) - 32768) <= 1 &&
              pixel.alpha() == 32768,
          "16-bit premultiplied images become straight-alpha samples without 8-bit quantization");
    QImage ten(1024, 1, QImage::Format_RGB30);
    for (int x = 0; x < 1024; ++x)
        reinterpret_cast<quint32 *>(ten.bits())[x] = (3u << 30) | (quint32(x) << 20);
    const QImage tenUpload = prepareImageUpload(ten);
    check(tenUpload.format() == QImage::Format_RGBA64, "10-bit packed images retain their source precision");
    const auto *tenPixels = reinterpret_cast<const QRgba64 *>(tenUpload.constBits());
    int unique = 1;
    for (int x = 1; x < 1024; ++x)
        if (tenPixels[x].red() > tenPixels[x - 1].red())
            ++unique;
    check(unique == 1024, "all 1024 packed RGB10 levels survive conversion");
    return failures ? 1 : 0;
}
