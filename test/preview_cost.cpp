// What the interface's pictures cost a frame (the OpenGL renderer; NYXMAP_RENDERER=rhi for QRhi): a 4K composition, the
// four preview cells and the mapping view's picture asked for or not. Prints the mean time of a frame.
#include "Engine.h"
#include "Layer.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    Engine e;
    QString err;
    if (!e.initialize(&err)) return 77;
    e.newProject();
    e.setCompositionSize(QSize(3840, 2160));
    QTemporaryDir tmp;
    QImage picture(3840, 2160, QImage::Format_RGB32);
    picture.fill(Qt::darkCyan);
    picture.save(tmp.path() + "/p.png");
    e.setLayerImage(e.addLayer(QStringLiteral("P")), tmp.path() + "/p.png", &err);
    auto run = [&](const char *what, bool asked) {
        for (int slot = 0; slot < 4; ++slot) e.setPreviewSource(slot, 0);
        for (int k = 0; k < 20; ++k) e.renderFrame();
        QElapsedTimer t;
        t.start();
        const int frames = 200;
        for (int k = 0; k < frames; ++k) {
            if (asked) e.wantCompositionImage();
            e.renderFrame();
        }
        std::printf("  %-40s %.2f ms per frame\n", what, t.nsecsElapsed() / 1e6 / frames);
    };
    run("pictures asked (4 cells + mapping view)", true);
    const QImage cell = e.previewImage(0), mapping = e.compositionImage();
    std::printf("  cell %dx%d, mapping view %dx%d\n", cell.width(), cell.height(), mapping.width(), mapping.height());
    e.shutdown();
    return 0;
}
