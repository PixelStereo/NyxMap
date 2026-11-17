// What ISF costs: N layers, each an ISF generator under some ISF effects, in a composition of the given size, rendered
// uncapped with the render thread running. Usage: nyxmap_isf_bench [layers] [effects per layer] [WxH] [seconds]
#include "Engine.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const int layers = argc > 1 ? QString(argv[1]).toInt() : 8;
    const int effects = argc > 2 ? QString(argv[2]).toInt() : 2;
    QSize size(1920, 1080);
    if (argc > 3) size = QSize(QString(argv[3]).section('x', 0, 0).toInt(), QString(argv[3]).section('x', 1, 1).toInt());
    const double seconds = argc > 4 ? QString(argv[4]).toDouble() : 5;
    Engine e;
    QString err;
    if (!e.initialize(&err)) { std::printf("no renderer: %s\n", qPrintable(err)); return 1; }
    e.newProject();
    e.setCompositionSize(size);
    e.setViewportSize(e.indexOfId(e.mainViewportId()), size);
    const QString root = QDir::current().filePath("isf");
    const QStringList gens = QDir(root + "/generators").entryList({"*.fs"}, QDir::Files);
    const QStringList fxs = {"Hue.fs", "Kaleidoscope.fs", "Pixelate.fs", "ColorCorrection.fs", "Separable Gaussian Blur.fs", "VHS Glitch.fs"};
    int loaded = 0;
    for (int i = 0; i < layers; ++i) {
        const int at = e.addLayer(QString("L%1").arg(i));
        if (!e.setLayerIsf(at, root + "/generators/" + gens[i % gens.size()], &err)) { std::printf("generator: %s\n", qPrintable(err)); continue; }
        e.setGeneratorSize(at, size.width(), size.height());
        for (int k = 0; k < effects; ++k) {
            const QString fx = root + "/effects/" + fxs[(i + k) % fxs.size()];
            if (QFile::exists(fx)) e.addEffect(at, fx, &err);
        }
        ++loaded;
    }
    std::printf("renderer %s, %d layers x %d effects at %dx%d\n", qPrintable(e.gpuRenderer()), loaded, effects, size.width(), size.height());
    e.start();
    Engine::RenderSettings r;
    r.frameRate = Engine::RenderSettings::UncappedFrameRate;
    if (qEnvironmentVariableIsSet("BENCH_DEPTH")) r.depth = qEnvironmentVariableIntValue("BENCH_DEPTH");
    e.setRenderSettings(r);
    QThread::msleep(2000);
    const auto before = e.frameTiming();
    QThread::msleep(int(seconds * 1000));
    const auto after = e.frameTiming();
    std::printf("ISF uncapped: %.1f fps, frame compute mean %.2f ms (p95 %.2f)\n", double(after.frames - before.frames) / seconds,
                after.compute.mean(), after.compute.percentile(0.95));
    e.stop();
    return 0;
}
