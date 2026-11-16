// A real TouchDesigner component through the engine (needs TouchDesigner installed and a component with an output TOP):
// the path in NYXMAP_TOX. Exit 77 (skipped) without it, without the SDK or without a QRhi backend when NYXMAP_RENDERER=rhi.
// Prints the component's status, its outputs and the picture's mean colour and centre pixel.
#include "Engine.h"
#include "Layer.h"
#include "Tox.h"

#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QThread>
#include <cstdio>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QString path = qEnvironmentVariable("NYXMAP_TOX");
    if (path.isEmpty() || !touchEngineCompiledIn()) {
        std::printf("SKIP: no NYXMAP_TOX, or no TouchEngine SDK\n");
        return 77;
    }
    Engine e;
    QString err;
    if (!e.initialize(&err)) {
        std::printf("SKIP: no OpenGL: %s\n", qPrintable(err));
        return 77;
    }
    e.newProject();
    e.setCompositionSize(QSize(320, 180));
    const int index = e.addLayer(QStringLiteral("Tox"));
    if (!e.setLayerTox(index, path, ToxSettings(), &err)) {
        std::printf("FAIL: %s\n", qPrintable(err));
        return 1;
    }
    const quint64 id = e.layerId(index);
    QImage picture;
    QString status;
    const int frames = qEnvironmentVariableIntValue("NYXMAP_TOX_SECONDS") * 50;
    for (int k = 0; k < (frames > 0 ? frames : 1500); ++k) { // 30 s, or as many seconds as asked
        e.renderFrame();
        QThread::msleep(20);
        QCoreApplication::processEvents(); // the SDK may call back through the main run loop
        Engine::Lock lock(&e.mutex());
        const Layer *l = e.layer(e.indexOfId(id));
        status = l->toxPlayer ? l->toxPlayer->status() : QString();
        if (k % 50 == 0) std::printf("  %s\n", qPrintable(status));
        picture = e.grabOutput();
        if (!picture.isNull() && l->srcWidth > 0) break;
    }
    {
        Engine::Lock lock(&e.mutex());
        const Layer *l = e.layer(e.indexOfId(id));
        if (l->toxPlayer) {
            for (const auto &o : l->toxPlayer->outputs()) std::printf("  output %s (%s)\n", qPrintable(o.first), qPrintable(o.second));
            std::printf("  error: %s\n", qPrintable(l->toxPlayer->error()));
        }
        std::printf("  source %d x %d\n", l->srcWidth, l->srcHeight);
    }
    int ok = 1;
    if (!picture.isNull()) {
        double sum = 0;
        for (int y = 0; y < picture.height(); ++y)
            for (int x = 0; x < picture.width(); ++x) sum += QColor(picture.pixel(x, y)).lightnessF();
        const QColor c(picture.pixel(picture.width() / 2, picture.height() / 2));
        std::printf("  mean lightness %.3f, centre %s\n", sum / (double(picture.width()) * picture.height()), qPrintable(c.name()));
        if (const QString out = qEnvironmentVariable("NYXMAP_TOX_PNG"); !out.isEmpty()) picture.save(out);
        ok = 0;
    }
    e.shutdown();
    std::printf(ok ? "FAIL: no picture\n" : "ok\n");
    return ok;
}
