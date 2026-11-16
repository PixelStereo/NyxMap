// The engine's frame through QRhi (RhiRenderer) against the same frame through OpenGL: two engines, the same scene,
// their viewports' and composition's pictures compared. The scene holds what the QRhi renderer ports so far (an image
// on a corner-pinned mesh, a paused video with its ROI, color and a masked effect, ISF generators, a triangle ROI, the
// blend modes but Difference, a turned viewport with its own output color, exposure and opacity, the composition
// opacity) and nothing that moves with time.
//
// nyxmap_rhi_engine_test [--dump DIR]   Exit 77 (skipped) without OpenGL or a QRhi backend.
#include "Engine.h"
#include "Layer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstdio>

static int g_failures = 0;

// The scene, built the same way in either engine. Returns the viewports' ids (the first: the whole composition)
static std::vector<quint64> build(Engine &e, const QString &root)
{
    e.newProject();
    e.setCompositionSize(QSize(640, 360));
    Engine::RenderSettings quality; // the mapped edges antialiased, the pictures drawn smaller than they are smoothed
    quality.samples = 4;
    quality.mipmaps = 1;
    e.setRenderSettings(quality);
    QString err;
    auto check = [&](bool ok, const char *what) {
        if (!ok) {
            std::printf("FAIL building the scene: %s %s\n", what, qPrintable(err));
            ++g_failures;
        }
    };
    const QString isf = root + "/../isf";
    {
        const int i = e.addLayer(QStringLiteral("Grid"));
        check(e.setLayerIsf(i, isf + "/generators/Color Test Grid.fs", &err), "Color Test Grid");
        Engine::Lock lk(&e.mutex());
        Layer &l = *e.layer(i);
        l.blend = BlendMode::Subtract;
        l.opacity = 0.3f;
    }
    {
        const int i = e.addLayer(QStringLiteral("Corners"));
        check(e.setLayerIsf(i, isf + "/generators/Corner Colors.fs", &err), "Corner Colors");
        Engine::Lock lk(&e.mutex());
        Layer &l = *e.layer(i);
        l.blend = BlendMode::Add;
        l.roiMode = 1; // a triangle of it
        l.roiPoints = {{{0.1, 0.1}, {0.9, 0.2}, {0.4, 0.9}, {0, 1}}};
        l.opacity = 0.7f;
    }
    {
        const int i = e.addLayer(QStringLiteral("Checker"));
        check(e.setLayerIsf(i, isf + "/generators/Checkerboard.fs", &err), "Checkerboard");
        Engine::Lock lk(&e.mutex());
        Layer &l = *e.layer(i);
        l.blend = BlendMode::Multiply;
        l.opacity = 0.6f;
        l.mapping.size = QSizeF(0.6, 0.5);
        l.mapping.position = QPointF(0.35, 0.6);
        l.mapping.rotation = 12;
    }
    {
        const int i = e.addLayer(QStringLiteral("Video"));
        check(e.setLayerVideo(i, root + "/media/av.mp4", &err), "av.mp4");
        e.setLayerPlaying(i, false);
        check(e.addEffect(i, isf + "/effects/Kaleidoscope.fs", &err) >= 0, "Kaleidoscope");
        Engine::Lock lk(&e.mutex());
        Layer &l = *e.layer(i);
        l.blend = BlendMode::Screen;
        l.roi = QRectF(0.1, 0.1, 0.7, 0.8);
        l.color.add[0] = 0.1f;
        l.color.remove[2] = 0.2f;
        IsfInstance &fx = *l.effects.back();
        if (IsfInput *in = fx.input(QStringLiteral("rotation"))) in->fValue = 17;
        if (IsfInput *in = fx.input(QStringLiteral("center"))) in->pValue = QPointF(0.42, 0.57);
        fx.mix = 0.7;
        fx.channels = {{true, true, false, true}};
    }
    {
        const int i = e.addLayer(QStringLiteral("Bars"));
        check(e.setLayerImage(i, root + "/media/bars.png", &err), "bars.png");
        Engine::Lock lk(&e.mutex());
        Layer &l = *e.layer(i);
        l.opacity = 0.85f;
        l.mapping.setCorner(0, QPointF(0.05, 0.1)); // corner-pinned: a mesh of its own
        l.mapping.setCorner(2, QPointF(0.8, 0.95));
        l.mapping.size = QSizeF(0.5, 0.5);
        l.mapping.position = QPointF(0.7, 0.3);
    }
    quint64 checker = 0, grid = 0;
    {
        Engine::Lock lk(&e.mutex());
        for (int i = 0; i < e.layerCount(); ++i) {
            if (e.layer(i)->name == QLatin1String("Checker")) checker = e.layer(i)->id;
            if (e.layer(i)->name == QLatin1String("Grid")) grid = e.layer(i)->id;
        }
    }
    { // masks: a soft ellipse on the picture, a rectangle where it lands in the composition
        Engine::Lock lk(&e.mutex());
        for (int i = 0; i < e.layerCount(); ++i) {
            Layer &l = *e.layer(i);
            if (l.name == QLatin1String("Checker")) {
                Mask m = Mask::ellipse(QRectF(0.15, 0.1, 0.7, 0.8));
                m.feather = 0.15;
                l.masks.push_back(m);
                ++l.masksRevision;
            } else if (l.name == QLatin1String("Corners")) {
                Mask m = Mask::rectangle(QRectF(0.05, 0.2, 0.6, 0.6));
                m.attach = Mask::Attach::Composition;
                m.feather = 0.05;
                l.masks.push_back(m);
                ++l.masksRevision;
            }
        }
    }
    { // Text: a heading with an outline and a shadow
        const int i = e.addLayer(QStringLiteral("Heading"));
        e.setLayerText(i);
        e.setLayerTextContent(i, QStringLiteral("QRhi draws\nthe text"));
        Engine::Lock lk(&e.mutex());
        Layer &l = *e.layer(i);
        l.text.size = 64;
        l.text.outline = 3;
        l.text.outlineColor = QColor(20, 40, 160);
        l.text.shadow = true;
        l.mapping.size = QSizeF(0.5, 0.4);
        l.mapping.position = QPointF(0.3, 0.3);
    }
    { // Difference over what is below, matted in Composition space by the Checker, and in Picture space by the Grid
        const int i = e.addLayer(QStringLiteral("Diff"));
        check(e.setLayerImage(i, root + "/media/bars.png", &err), "bars.png (Difference)");
        Engine::Lock lk(&e.mutex());
        Layer &l = *e.layer(i);
        l.blend = BlendMode::Difference;
        l.opacity = 0.8f;
        l.mapping.size = QSizeF(0.7, 0.6);
        l.mapping.position = QPointF(0.4, 0.55);
    }
    {
        const int i = e.addLayer(QStringLiteral("Matted"));
        check(e.setLayerImage(i, root + "/media/bars.png", &err), "bars.png (matted)");
        Engine::Lock lk(&e.mutex());
        Layer &l = *e.layer(i);
        l.mapping.size = QSizeF(0.5, 0.5);
        l.mapping.position = QPointF(0.3, 0.7);
        l.matte.layer = checker;
        l.matte.space = LayerMatte::Space::Composition;
        l.matte.invert = true;
    }
    {
        const int i = e.addLayer(QStringLiteral("Picture matted"));
        check(e.setLayerImage(i, root + "/media/bars.png", &err), "bars.png (picture matte)");
        Engine::Lock lk(&e.mutex());
        Layer &l = *e.layer(i);
        l.mapping.size = QSizeF(0.4, 0.4);
        l.mapping.position = QPointF(0.75, 0.75);
        l.matte.layer = grid;
        l.matte.space = LayerMatte::Space::Picture;
        l.matte.channel = LayerMatte::Channel::Alpha;
    }
    {
        const int v = e.addViewport(QStringLiteral("Turned"), QSize(320, 240));
        Engine::Lock lk(&e.mutex());
        Layer &l = *e.layer(v);
        l.mapping.size = QSizeF(0.6, 0.7);
        l.mapping.position = QPointF(0.45, 0.5);
        l.mapping.rotation = 20;
        l.outputSpace = QStringLiteral("displayp3");
        l.outputExposure = 0.5;
        l.opacity = 0.9f;
    }
    e.fadeCompositionOpacity(0.8, 0);
    std::vector<quint64> viewports;
    Engine::Lock lk(&e.mutex());
    for (int i = 0; i < e.layerCount(); ++i)
        if (e.layer(i)->isViewport) viewports.push_back(e.layer(i)->id);
    return viewports;
}

// A source transition caught half way, from one picture to another: a crossfade, or a transition shader
static QImage transitionFrame(Engine &e, const QString &shader, const QString &dir)
{
    e.newProject();
    e.setFadesManual(true); // its time moves by advanceFades only: the same progress in either engine
    e.setCompositionSize(QSize(320, 180));
    e.setDefaultTransition(shader);
    QImage from(160, 90, QImage::Format_RGBA8888), to(160, 90, QImage::Format_RGBA8888);
    for (int y = 0; y < 90; ++y)
        for (int x = 0; x < 160; ++x) {
            from.setPixelColor(x, y, QColor(220, 30 + y, 40));
            to.setPixelColor(x, y, QColor(20, 40 + x, 200 - y));
        }
    const QString a = dir + "/from.png", b = dir + "/to.png";
    QString err;
    const int i = e.addLayer(QStringLiteral("T"));
    if (!from.save(a) || !to.save(b) || !e.setLayerImage(i, b, &err)) {
        std::printf("FAIL building the transition: %s\n", qPrintable(err));
        ++g_failures;
        return {};
    }
    Engine::Snapshot snap;
    snap.name = "To";
    snap.fade = 2;
    snap.layers = e.captureLayers();
    const int index = e.addSnapshot(snap);
    e.setLayerImage(i, a, &err);
    for (int k = 0; k < 5; ++k) {
        e.renderFrame();
        QThread::msleep(10);
    }
    e.recallSnapshot(index);
    e.advanceFades(0.8);
    for (int k = 0; k < 3; ++k) e.renderFrame();
    return e.grabOutput();
}

struct Diff {
    int max = 0;
    double mean = 0, over8 = 0;
};
static Diff compare(const QImage &a, const QImage &b)
{
    Diff d;
    if (a.isNull() || a.size() != b.size()) {
        d.max = 255, d.mean = 255, d.over8 = 1;
        return d;
    }
    const QImage x = a.convertToFormat(QImage::Format_RGBA8888), y = b.convertToFormat(QImage::Format_RGBA8888);
    double sum = 0, over = 0;
    for (int r = 0; r < x.height(); ++r) {
        const uchar *p = x.constScanLine(r), *q = y.constScanLine(r);
        for (int c = 0; c < x.width() * 4; ++c) {
            const int v = std::abs(int(p[c]) - int(q[c]));
            d.max = std::max(d.max, v);
            sum += v;
            over += v > 8;
        }
    }
    const double n = double(x.width()) * x.height() * 4;
    d.mean = sum / n;
    d.over8 = over / n;
    return d;
}

int main(int argc, char **argv)
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QGuiApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    QString dump;
    if (const qsizetype i = args.indexOf(QStringLiteral("--dump")); i >= 0 && i + 1 < args.size()) {
        dump = args.at(i + 1);
        QDir().mkpath(dump);
    }
    const QString root = QStringLiteral(TEST_DIR);

    Engine viaGl, viaRhi;
    viaRhi.setRhiRendering(true);
    QString err;
    if (!viaGl.initialize(&err) || !viaRhi.initialize(&err)) {
        std::printf("SKIP: no OpenGL: %s\n", qPrintable(err));
        return 77;
    }
    const std::vector<quint64> glViews = build(viaGl, root), rhiViews = build(viaRhi, root);
    if (g_failures) return 1;
    // A few frames: the video's first frame decoded and shown, the images uploaded
    for (int k = 0; k < 30; ++k) {
        viaGl.renderFrame();
        viaRhi.renderFrame();
        QThread::msleep(10);
    }
    if (!viaRhi.rhiRendering()) {
        std::printf("SKIP: no QRhi backend\n");
        return 77;
    }
    struct Picture {
        QString name;
        QImage gl, rhi;
    };
    std::vector<Picture> pictures{{QStringLiteral("composition"), viaGl.grabOutput(), viaRhi.grabOutput()}};
    for (size_t k = 0; k < glViews.size() && k < rhiViews.size(); ++k)
        pictures.push_back({QStringLiteral("viewport %1").arg(k + 1), viaGl.grabViewport(glViews[k]), viaRhi.grabViewport(rhiViews[k])});
    for (const Picture &p : pictures) {
        const Diff d = compare(p.rhi, p.gl);
        // The same picture: rounding, and a few edge pixels filtered apart on some GPUs
        const bool same = d.mean <= 0.5 && d.over8 <= 0.005;
        std::printf("  %-12s %dx%d  QRhi against OpenGL: max %3d mean %.3f, %.2f%% over 8%s\n", qPrintable(p.name), p.gl.width(),
                    p.gl.height(), d.max, d.mean, d.over8 * 100, same ? "" : "  DIFFERENT");
        if (!same) ++g_failures;
        if (!dump.isEmpty()) {
            p.rhi.save(dump + "/" + p.name + ".rhi.png");
            p.gl.save(dump + "/" + p.name + ".gl.png");
        }
    }
    // Antialiasing and mipmaps did their work: smoothed pictures made, and the edges differ from an unsmoothed frame's
    {
        std::printf("  mipmaps made: QRhi %llu, OpenGL %llu\n", (unsigned long long)viaRhi.mipmapsMade(), (unsigned long long)viaGl.mipmapsMade());
        if (!viaRhi.mipmapsMade()) {
            std::printf("FAIL: the QRhi renderer made no mipmaps\n");
            ++g_failures;
        }
        Engine::RenderSettings plain;
        plain.samples = 0;
        plain.mipmaps = 0;
        viaRhi.setRenderSettings(plain);
        for (int k = 0; k < 3; ++k) viaRhi.renderFrame();
        const Diff d = compare(viaRhi.grabOutput(), pictures.front().rhi);
        std::printf("  without antialiasing or mipmaps: max %3d mean %.3f\n", d.max, d.mean);
        if (d.max <= 8) {
            std::printf("FAIL: antialiasing and mipmaps changed nothing\n");
            ++g_failures;
        }
    }
    QTemporaryDir files;
    for (const QString &shader : {QString(), root + "/../isf/transitions/Angular.fs"}) {
        const QImage gl = transitionFrame(viaGl, shader, files.path()), rhi = transitionFrame(viaRhi, shader, files.path());
        const Diff d = compare(rhi, gl);
        const bool same = d.mean <= 0.5 && d.over8 <= 0.005 && !gl.isNull();
        std::printf("  %-12s %dx%d  QRhi against OpenGL: max %3d mean %.3f, %.2f%% over 8%s\n",
                    shader.isEmpty() ? "crossfade" : "transition", gl.width(), gl.height(), d.max, d.mean, d.over8 * 100,
                    same ? "" : "  DIFFERENT");
        if (!same) ++g_failures;
        if (!dump.isEmpty()) {
            rhi.save(dump + (shader.isEmpty() ? "/crossfade.rhi.png" : "/transition.rhi.png"));
            gl.save(dump + (shader.isEmpty() ? "/crossfade.gl.png" : "/transition.gl.png"));
        }
    }
    std::printf(g_failures ? "%d failure(s)\n" : "ok\n", g_failures);
    return g_failures ? 1 : 0;
}
