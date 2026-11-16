// The interface's previews with the QRhi renderer: its slots give the QRhi pictures read back, as pixels. The composition's and a layer's previews are compared with the pictures the engine gives out.
//
// nyxmap_rhi_preview_test   Exit 77 (skipped) without OpenGL or a QRhi backend.
#include "Engine.h"
#include <rhi/qrhi.h>
#include "Layer.h"
#include "LayerTree.h"
#include "Model3D.h"
#include "LiveInput.h"
#include "Publish.h"
#include "Tox.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <cmath>
#include <mutex>
#include <cstdio>

static int g_failures = 0;
static void check(bool ok, const char *what)
{
    std::printf("  %s%s\n", what, ok ? "" : "  FAIL");
    g_failures += !ok;
}

// A preview slot's picture as an image, top row first (the engine gives it bottom row first)
static QImage readSlot(Engine &e, int slot) { return e.previewImage(slot).flipped(Qt::Vertical); }

static double meanDifference(const QImage &a, const QImage &b)
{
    if (a.isNull() || a.size() != b.size()) return 255;
    const QImage x = a.convertToFormat(QImage::Format_RGBA8888), y = b.convertToFormat(QImage::Format_RGBA8888);
    double sum = 0;
    for (int r = 0; r < x.height(); ++r) {
        const uchar *p = x.constScanLine(r), *q = y.constScanLine(r);
        for (int c = 0; c < x.width() * 4; ++c) sum += std::abs(int(p[c]) - int(q[c]));
    }
    return sum / (double(x.width()) * x.height() * 4);
}

namespace {
// A component that gives a green picture and records the texture it is given for its input
struct FakeTox final : ToxPlayer {
    static inline QRhiTexture *inputTexture = nullptr;
    static inline int inputWidth = 0;
    void configure(const ToxSettings &) override {}
    void reload() override {}
    void pulse(const QString &) override {}
    std::vector<ToxControl> controls() const override { return {}; }
    std::vector<std::pair<QString, QString>> inputs() const override { return {{"in1", "Image"}}; }
    std::vector<std::pair<QString, QString>> outputs() const override { return {{"out1", "Result"}}; }
    quint64 revision() const override { return 1; }
    QString error() const override { return {}; }
    void setInputRhiTexture(const QString &, QRhiTexture *texture) override
    {
        inputTexture = texture;
        inputWidth = texture ? texture->pixelSize().width() : 0;
    }
    bool takeFrame(QImage *out) override
    {
        *out = QImage(16, 16, QImage::Format_RGBA8888);
        out->fill(QColor(0, 200, 0));
        return true;
    }
};
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    Engine e;
    QString err;
    if (!e.initialize(&err)) {
        std::printf("SKIP: no OpenGL: %s\n", qPrintable(err));
        return 77;
    }
    e.newProject();
    e.setCompositionSize(QSize(320, 180));
    const int li = e.addLayer(QStringLiteral("Bars"));
    if (!e.setLayerImage(li, QStringLiteral(TEST_DIR) + "/media/bars.png", &err)) {
        std::printf("FAIL: bars.png: %s\n", qPrintable(err));
        return 1;
    }
    const quint64 id = e.layerId(li);
    e.setPreviewSource(0, 0);  // the composition
    e.setPreviewSource(1, id); // a layer
    for (int k = 0; k < 10; ++k) {
        e.renderFrame();
        QThread::msleep(10);
    }
    if (!e.rhiRendering()) {
        std::printf("SKIP: no QRhi backend\n");
        return 77;
    }
    const QImage composition = readSlot(e, 0), layer = readSlot(e, 1);
    check(!composition.isNull(), "the composition's preview is there");
    check(!layer.isNull(), "the layer's preview is there");
    const double a = meanDifference(composition, e.grabOutput());
    std::printf("  composition preview against the composition: mean %.3f\n", a);
    check(a < 1.0, "the composition's preview is its picture, the right way up");
    // The mapping view's picture of the composition, made while it asks for it
    QImage mapping;
    for (int k = 0; k < 5 && mapping.isNull(); ++k) {
        e.wantCompositionImage();
        e.renderFrame();
        mapping = e.compositionImage();
    }
    check(!mapping.isNull() && meanDifference(mapping.flipped(Qt::Vertical), e.grabOutput()) < 1.0, "the mapping view gets the composition");
    // A layer that is not there shows nothing, never a stale picture
    e.setPreviewSource(2, 987654321);
    for (int k = 0; k < 3; ++k) e.renderFrame();
    check(e.previewImage(2).isNull(), "a layer that is not there shows nothing");
    // The inspector's source preview: the layer's picture before its ROI, as the OpenGL renderer gives it
    e.requestSourcePreview(id, 200);
    QImage source;
    quint64 sourceId = 0;
    for (int k = 0; k < 40 && source.isNull(); ++k) {
        e.renderFrame();
        QThread::msleep(20);
        source = e.sourcePreview(&sourceId);
    }
    check(!source.isNull() && sourceId == id, "the source preview comes back");
    check(!source.isNull() && std::max(source.width(), source.height()) == 200, "reduced to the side asked");
    e.requestSourcePreview(0);

    // Pixel mapping: the composition sampled at the fixtures' pixels (a red left half, a blue right half)
    QTemporaryDir tmp;
    QImage halves(64, 32, QImage::Format_RGB32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) halves.setPixel(x, y, x < 32 ? qRgb(255, 0, 0) : qRgb(0, 0, 255));
    const QString halvesPath = tmp.path() + "/halves.png";
    halves.save(halvesPath);
    e.newProject();
    e.setCompositionSize(QSize(64, 32));
    e.setLayerImage(e.addLayer(QStringLiteral("Halves")), halvesPath, &err);
    FixturePatch patch;
    FixtureType pair;
    pair.id = "pair";
    pair.cols = 2;
    pair.perPixel = {{ChannelRole::Red}, {ChannelRole::Green}, {ChannelRole::Blue}};
    patch.types.push_back(pair);
    DmxOutput output;
    output.id = 1;
    output.enabled = false; // nothing on the network here
    patch.outputs.push_back(output);
    Fixture fixture;
    fixture.type = "pair";
    fixture.output = 1;
    fixture.universe = 5;
    fixture.address = 100;
    fixture.size = {1.0, 0.5};
    patch.fixtures.push_back(fixture);
    e.setFixturePatch(patch);
    QByteArray u;
    for (int k = 0; k < 60; ++k) {
        e.renderFrame();
        u = e.dmxUniverse(1, 5);
        if (u.size() == 512 && quint8(u.at(99)) == 255) break;
    }
    auto at = [&](int address) { return u.size() == 512 ? int(quint8(u.at(address - 1))) : -1; };
    check(at(100) == 255 && at(101) == 0 && at(102) == 0, "pixel mapping: the left pixel is red");
    check(at(103) == 0 && at(104) == 0 && at(105) == 255, "pixel mapping: the right pixel is blue");

    // Publishing: the viewport's picture goes to NDI-like (CPU) publishers as BGRA, top row first, and to Syphon-like (GPU)
    // publishers as an OpenGL texture
    e.addViewport(QStringLiteral("Out"), QSize(64, 32));
    struct Tap {
        std::mutex m;
        CpuFrame last;
        int frames = 0;
    };
    auto tap = std::make_shared<Tap>();
    e.setTestTap([tap](const CpuFrame &f) {
        std::lock_guard<std::mutex> lk(tap->m);
        tap->last = f;
        ++tap->frames;
    });
    struct Gpu : GpuPublisher {
        std::atomic<int> *count;
        std::atomic<int> *side;
        explicit Gpu(std::atomic<int> *c, std::atomic<int> *w) : count(c), side(w) {}
        bool start(const QString &, QString *) override { return true; }
        void publish(const unsigned char *pixels, int width, int) override
        {
            if (pixels) {
                ++*count;
                *side = width;
            }
        }
    };
    std::atomic<int> shares{0}, shareWidth{0};
    e.setGpuPublisherFactoryForTests([&](PublishKind) { return std::unique_ptr<GpuPublisher>(new Gpu(&shares, &shareWidth)); });
    PublishSettings ps;
    ps[PublishKind::Syphon].enabled = true;
    e.setPublishSettings(e.mainViewportId(), ps);
    for (int k = 0; k < 60; ++k) {
        e.renderFrame();
        QThread::msleep(10);
        std::lock_guard<std::mutex> lk(tap->m);
        if (tap->frames >= 5 && shares >= 3) break;
    }
    {
        std::lock_guard<std::mutex> lk(tap->m);
        const CpuFrame &f = tap->last;
        check(tap->frames >= 5 && f.width > 0 && f.height > 0 && f.stride == f.width * 4, "publishing: the tap receives the viewport's pictures");
        if (f.width > 0 && f.height > 0) {
            auto px = [&](int x, int y) { return QColor(f.bgra[size_t(y) * f.stride + x * 4 + 2], f.bgra[size_t(y) * f.stride + x * 4 + 1], f.bgra[size_t(y) * f.stride + x * 4]); };
            check(px(f.width / 6, f.height / 3) == QColor(255, 0, 0) && px(f.width * 5 / 6, f.height / 3) == QColor(0, 0, 255), "publishing: BGRA, the left half red, the right blue");
        }
    }
    check(shares >= 3 && shareWidth > 0, "publishing: a Syphon-like publisher gets the picture as a texture");
    e.setTestTap(nullptr);

    // A live input (a solid colour here): its pictures reach the QRhi frame
    e.newProject();
    e.setCompositionSize(QSize(64, 32));
    const int input = e.addLayer(QStringLiteral("Live"));
    check(e.setLayerInput(input, QStringLiteral("input:test:0,200,0"), &err), "live input: the layer takes it");
    QImage live;
    for (int k = 0; k < 60; ++k) {
        e.renderFrame();
        QThread::msleep(10);
        live = e.grabOutput();
        if (!live.isNull() && QColor(live.pixel(32, 16)).green() > 150) break;
    }
    check(!live.isNull() && QColor(live.pixel(32, 16)) == QColor(0, 200, 0), "live input: its colour is in the composition");

    // A Syphon server's picture (macOS: another engine publishes a green layer) reaches the QRhi frame as a texture
    {
        Engine server;
        QString serr;
        if (server.initialize(&serr)) {
            server.newProject();
            server.setCompositionSize(QSize(64, 32));
            QImage stripes(64, 32, QImage::Format_RGB32); // red above, blue below
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 64; ++x) stripes.setPixel(x, y, y < 16 ? qRgb(255, 0, 0) : qRgb(0, 0, 255));
            stripes.save(tmp.path() + "/stripes.png");
            server.setLayerImage(server.addLayer(QStringLiteral("Stripes")), tmp.path() + "/stripes.png", &serr);
            server.addViewport(QStringLiteral("Out"), QSize(64, 32));
            PublishSettings sp;
            sp[PublishKind::Syphon].enabled = true;
            sp[PublishKind::Syphon].name = QStringLiteral("NyxMapRhiTest");
            server.setPublishSettings(server.mainViewportId(), sp);
            QString uri;
            for (int k = 0; k < 40 && uri.isEmpty(); ++k) {
                server.renderFrame();
                QThread::msleep(25);
                QCoreApplication::processEvents(); // the directory hears the servers through notifications
                for (const LiveInputInfo &i : listLiveInputs())
                    if (i.kind == QLatin1String("Syphon") && i.name.endsWith(QLatin1String("NyxMapRhiTest"))) uri = i.uri;
            }
            if (uri.isEmpty()) {
                std::printf("  (no Syphon here: not checked)\n");
            } else {
                e.newProject();
                e.setCompositionSize(QSize(64, 32));
                check(e.setLayerInput(e.addLayer(QStringLiteral("Syphon")), uri, &serr), "syphon input: the layer takes it");
                QImage got;
                for (int k = 0; k < 80; ++k) {
                    server.renderFrame();
                    e.renderFrame();
                    QThread::msleep(15);
                    QCoreApplication::processEvents();
                    got = e.grabOutput();
                    if (!got.isNull() && QColor(got.pixel(32, 4)).red() > 150) break;
                }
                check(!got.isNull() && QColor(got.pixel(32, 4)) == QColor(255, 0, 0) && QColor(got.pixel(32, 28)) == QColor(0, 0, 255),
                      "syphon input: the server's picture is in the composition, the right way up");
            }
            server.shutdown();
        }
    }

    // A TouchEngine layer (a component that gives green): its picture reaches the frame, and its input gets a layer's
    {
        setToxFactoryForTests([](const QString &, const ToxSettings &) { return std::make_unique<FakeTox>(); });
        e.newProject();
        e.setCompositionSize(QSize(64, 32));
        const int source = e.addLayer(QStringLiteral("Source"));
        e.setLayerImage(source, halvesPath, &err);
        const quint64 sourceId = e.layerId(source);
        QFile component(tmp.path() + "/sample.tox");
        component.open(QIODevice::WriteOnly);
        component.write("tox");
        component.close();
        const int toxAt = e.addLayer(QStringLiteral("Tox"));
        const quint64 toxId = e.layerId(toxAt);
        const int tox = e.indexOfId(toxId); // adding a layer moves the others
        ToxSettings ts;
        ts.output = "out1";
        check(e.setLayerTox(tox, component.fileName(), ts, &err), "tox: the layer takes the component");
        check(e.setLayerToxInput(e.indexOfId(toxId), QStringLiteral("in1"), sourceId, &err), "tox: its input takes the source layer");
        QImage got;
        for (int k = 0; k < 40; ++k) {
            e.renderFrame();
            QThread::msleep(10);
            got = e.grabOutput();
            if (!got.isNull() && QColor(got.pixel(32, 16)).green() > 150 && FakeTox::inputTexture) break;
        }
        check(!got.isNull() && QColor(got.pixel(32, 16)) == QColor(0, 200, 0), "tox: its picture is in the composition");
        check(FakeTox::inputTexture != nullptr && FakeTox::inputWidth == 64, "tox: its input gets the source layer's picture");
        setToxFactoryForTests({});
    }

    // A 3D view of a scene (a cell showing a primitive from the view's own camera), through QRhi
    {
        e.newProject();
        e.setCompositionSize(QSize(64, 32));
        const int scene = e.addScene(QStringLiteral("Stage"));
        const quint64 sceneId = e.layerId(scene);
        const int model = e.addLayer(QStringLiteral("Ball"), scene + 1);
        const quint64 modelId = e.layerId(model);
        e.setLayerModel(model, Model3D::primitivePath(QStringLiteral("Sphere")), &err);
        e.setStructure(tree::intoGroup(e.structure(), {modelId}, sceneId));
        SceneSettings camera;
        camera.distance = 3;
        camera.background = {0.1f, 0.1f, 0.1f, 1};
        e.setPreviewSource(0, sceneId);
        e.setPreviewScene(0, true, camera, QSize(160, 120), Scene3D::ViewLook::Unlit);
        QImage view;
        for (int k = 0; k < 20 && view.isNull(); ++k) {
            e.renderFrame();
            QThread::msleep(10);
            view = e.previewImage(0);
        }
        check(!view.isNull() && view.size() == QSize(160, 120), "3D view: its picture comes back, at the size asked");
        const QColor middle = view.isNull() ? QColor() : QColor(view.pixel(80, 60));
        check(view.isNull() ? false : (middle.red() > 120 || middle.green() > 120 || middle.blue() > 120), "3D view: the model is in it");
        // A layer's picture laid on the model (red on the left half of the picture, blue on the right)
        const int pictureAt = e.addLayer(QStringLiteral("Picture"));
        const quint64 pictureId = e.layerId(pictureAt);
        e.setLayerImage(pictureAt, halvesPath, &err);
        check(e.setModelPicture(e.indexOfId(modelId), pictureId, &err), "3D view: a picture is laid on the model");
        QImage laid;
        for (int k = 0; k < 20; ++k) {
            e.renderFrame();
            QThread::msleep(10);
            laid = e.previewImage(0);
        }
        bool reds = false, blues = false;
        for (int y = 40; y < 80 && !laid.isNull(); y += 4)
            for (int x = 50; x < 110; x += 4) {
                const QColor c(laid.pixel(x, y));
                reds |= c.red() > 150 && c.blue() < 90;
                blues |= c.blue() > 150 && c.red() < 90;
            }
        check(reds && blues, "3D view: the picture's red and blue are on the model");
        // The scene itself, in the composition: lit by a light
        const int lampAt = e.addLayer(QStringLiteral("Key"), e.indexOfId(modelId) + 1);
        const quint64 lampId = e.layerId(lampAt);
        e.setLayerLight(lampAt);
        e.setStructure(tree::intoGroup(e.structure(), {lampId}, sceneId));
        QImage composed;
        for (int k = 0; k < 10; ++k) {
            e.renderFrame();
            composed = e.grabOutput();
        }
        const QColor centre = composed.isNull() ? QColor() : QColor(composed.pixel(composed.width() / 2, composed.height() / 2));
        check(!composed.isNull() && (centre.red() + centre.green() + centre.blue()) > 60, "3D scene: lit, it is in the composition");
    }
    e.shutdown();
    std::printf(g_failures ? "%d failure(s)\n" : "ok\n", g_failures);
    return g_failures ? 1 : 0;
}
