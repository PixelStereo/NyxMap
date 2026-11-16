// An output window shown through the QRhi renderer: a swapchain of the platform's QRhi, the viewport's picture drawn
// into it. The picture it presented (read back from the swapchain) is compared with the viewport's own picture,
// scaled to the window: the same one, the right way up.
//
// nyxmap_rhi_output_test [--dump DIR]   Exit 77 (skipped) without a QRhi backend or when the window is not exposed.
#include "Engine.h"
#include "RhiDevice.h"
#include "Layer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QSurfaceFormat>
#include <QThread>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv)
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QGuiApplication app(argc, argv);
    QString dump;
    const QStringList args = app.arguments().mid(1);
    if (const qsizetype i = args.indexOf(QStringLiteral("--dump")); i >= 0 && i + 1 < args.size()) {
        dump = args.at(i + 1);
        QDir().mkpath(dump);
    }
    const QString root = QStringLiteral(TEST_DIR);

    {
        const std::unique_ptr<QRhi> probe = rhidevice::create();
        if (probe && rhidevice::isVirtualDevice(probe.get())) {
            std::printf("SKIP: %s is a virtual GPU, its pictures are not OpenGL's to the pixel\n", probe->driverInfo().deviceName.constData());
            return 77;
        }
    }
    Engine e;
    e.setRhiRendering(true);
    QString err;
    if (!e.initialize(&err)) {
        std::printf("SKIP: no OpenGL: %s\n", qPrintable(err));
        return 77;
    }
    e.newProject();
    e.setCompositionSize(QSize(320, 180));
    const int vi = e.indexOfId(e.mainViewportId());
    e.setViewportSize(vi, QSize(320, 180));
    const int li = e.addLayer(QStringLiteral("Bars"));
    if (!e.setLayerImage(li, root + "/media/bars.png", &err)) {
        std::printf("FAIL: bars.png: %s\n", qPrintable(err));
        return 1;
    }
    const quint64 viewport = e.mainViewportId();
    for (int k = 0; k < 10; ++k) {
        e.renderFrame();
        QThread::msleep(10);
    }
    if (!e.rhiRendering()) {
        std::printf("SKIP: no QRhi backend\n");
        return 77;
    }

    QWindow window;
    window.setSurfaceType(e.outputSurfaceType());
    if (window.surfaceType() == QSurface::OpenGLSurface) {
        std::printf("SKIP: no native surface type for the QRhi backend\n");
        return 77;
    }
    window.resize(320, 180);
    window.create();
    e.setViewportWindow(viewport, &window);
    window.show();
    QElapsedTimer waited;
    waited.start();
    while (!window.isExposed() && waited.elapsed() < 3000) {
        app.processEvents();
        QThread::msleep(10);
    }
    if (!window.isExposed()) {
        std::printf("SKIP: the window is not exposed\n");
        return 77;
    }
    const QSize pixels = window.size() * window.devicePixelRatio();
    e.setCapturePresentedForTests(true);
    e.setViewportExposed(viewport, true, pixels, 60);
    for (int k = 0; k < 8; ++k) {
        e.renderFrame();
        app.processEvents();
        QThread::msleep(10);
    }
    int failures = 0;
    auto check = [&](bool ok, const char *what) {
        std::printf("  %s%s\n", what, ok ? "" : "  FAIL");
        failures += !ok;
    };
    check(e.windowFrames(viewport) >= 4, "the window was given pictures");
    const QImage shown = e.presentedForTests(viewport);
    const QImage own = e.grabViewport(viewport);
    check(!shown.isNull() && !own.isNull(), "the presented picture was read back");
    if (!shown.isNull() && !own.isNull()) {
        const QImage expected = own.scaled(shown.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
        double sum = 0;
        for (int y = 0; y < shown.height(); ++y) {
            const uchar *a = shown.constScanLine(y), *b = expected.constScanLine(y);
            for (int x = 0; x < shown.width() * 4; ++x) sum += std::abs(int(a[x]) - int(b[x]));
        }
        const double mean = sum / (double(shown.width()) * shown.height() * 4);
        std::printf("  %dx%d presented against the viewport's picture: mean %.3f\n", shown.width(), shown.height(), mean);
        check(mean < 4.0, "the window shows the viewport's picture, the right way up");
        if (!dump.isEmpty()) {
            shown.save(dump + "/presented.png");
            expected.save(dump + "/expected.png");
        }
    }
    e.setViewportWindow(viewport, nullptr);
    e.shutdown();
    std::printf(failures ? "%d failure(s)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
