// The Line generator: its shader built in (no file), lines drawn on the layer and animated along them (solid, fill,
// chase, strobe, offset per line), the segments it reads, and a project that keeps it and its lines.
#include "Engine.h"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QSet>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <cstdio>

static int failures = 0;
static void check(bool ok, const char *why)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL line_generator: %s\n", why);
    }
}

static constexpr int kW = 64, kH = 32;

static bool lit(const QImage &img, int x, int y) { return qGray(img.pixel(x, y)) > 200; }
static bool dark(const QImage &img, int x, int y) { return qGray(img.pixel(x, y)) < 55; }

static LinePath path(std::initializer_list<QPointF> pts, bool closed = false)
{
    LinePath l;
    l.points = pts;
    l.closed = closed;
    return l;
}

static void modelTests()
{
    check(isLineGeneratorPath(kLineGeneratorPath) && isLineGeneratorPath("C:\\Pinol\\Generators\\Line") &&
              !isLineGeneratorPath("/tmp/Lines.fs"),
          "its virtual path is recognised");
    const IsfInstance::Header h = IsfInstance::readHeader(kLineGeneratorPath);
    check(h.ok && !h.isFilter && !h.isTransition, "its built-in shader reads as an ISF generator");

    const LinePath open = path({{0, 0}, {1, 0}, {1, 1}});
    check(open.segmentCount() == 2 && path({{0, 0}, {1, 0}, {1, 1}}, true).segmentCount() == 3, "segments of a line");
    check(qFuzzyCompare(open.length(100, 50), 150.0), "its length in pixels (the frame is not square)");
    const auto pts = open.spread(3, 100, 50); // 150 px: at 25, 75, 125
    check(pts.size() == 3 && qFuzzyCompare(pts[0].x(), 0.25) && pts[1] == QPointF(0.75, 0) &&
              qFuzzyCompare(pts[2].y(), 0.5) && qFuzzyCompare(pts[2].x(), 1.0),
          "points spread along it by length");

    int count = 0;
    const std::vector<float> data = lineSegmentData({open, path({{0.5, 0.5}, {0.6, 0.5}})}, 100, 50, &count);
    check(count == 3 && data.size() == 24, "a texture of 3 segments, 2 rows");
    check(data[12 + 0] == 0 && qFuzzyCompare(data[12 + 1], 100.0f / 150) && data[12 + 6] == 0.0f && data[12 + 7] == 1.0f &&
              data[12 + 10] == 1.0f,
          "where each segment starts and ends along its line, its line, its index");
    lineSegmentData({}, 10, 10, &count);
    check(count == 1, "no line: one segment that is none");

    const auto back = linePathsFromJson(linePathsToJson({open, path({{0.1, 0.2}, {0.3, 0.4}, {0.5, 0.1}}, true)}));
    check(back.size() == 2 && back[0] == open && back[1].closed, "lines survive JSON");
    check(linePathsFromJson(QJsonDocument::fromJson(R"([{"points":[[0.1,0.1]]},{"points":"x"}])").array()).empty(),
          "a line of fewer than two points is dropped");
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QGuiApplication app(argc, argv);

    modelTests();

    Engine e;
    QString err;
    if (!e.initialize(&err)) {
        std::printf("FAIL OpenGL: %s\n", qPrintable(err));
        return 1;
    }
    e.newProject();
    e.setCompositionSize(QSize(kW, kH));
    e.setViewportSize(e.indexOfId(e.mainViewportId()), QSize(kW, kH));
    e.fadeCompositionOpacity(1.0, 0.0);
    const int layer = e.addLayer(QStringLiteral("Line"));
    check(e.setLayerIsf(layer, kLineGeneratorPath, &err), "a layer loads it");
    if (!err.isEmpty()) std::printf("%s\n", qPrintable(err));
    e.setGeneratorSize(layer, kW, kH);
    const quint64 id = e.layerId(layer);
    auto input = [&](const char *name) {
        Engine::Lock lk(&e.mutex());
        return e.layer(layer)->generator ? e.layer(layer)->generator->input(QString::fromLatin1(name)) : nullptr;
    };
    auto capture = [&] {
        for (int i = 0; i < 3; ++i) e.renderFrame();
        return e.grabViewport(e.mainViewportId()).convertToFormat(QImage::Format_RGB32);
    };
    check(input("animation") && input("thickness") && input("rate") && input("cascade") && input("media"),
          "its settings are the layer's parameters");
    if (!input("animation")) return 1;
    {
        Engine::Lock lk(&e.mutex());
        QSet<QString> seen;
        bool unique = true;
        for (const Parameter *p : e.layer(layer)->parameters()) {
            if (seen.contains(p->path())) std::printf("duplicate: %s\n", qPrintable(p->path()));
            unique &= !seen.contains(p->path());
            seen.insert(p->path());
        }
        check(unique && seen.contains("param/thickness") && seen.contains("speed"), "its parameters' addresses are its own");
    }

    // No line: nothing drawn (the background is transparent)
    input("animation")->lValue = 0;
    QImage img = capture();
    check(dark(img, 32, 16), "no line: nothing");

    // A horizontal line a quarter down (its frame's y down), 8 px thick: on it, lit; off it, dark
    e.setLayerLines(id, {path({{0.1, 0.25}, {0.9, 0.25}})});
    check(e.layerLines(id).size() == 1, "the layer has its line");
    img = capture();
    check(lit(img, 32, 8) && lit(img, 10, 8) && lit(img, 32, 6), "solid: lit along the line (a quarter down)");
    check(dark(img, 32, 16) && dark(img, 0, 8) && dark(img, 63, 8) && dark(img, 32, 24), "solid: dark off it");

    // Thickness
    input("thickness")->fValue = 2;
    img = capture();
    check(lit(img, 32, 8) && dark(img, 32, 5), "thinner");
    input("thickness")->fValue = 8;

    // A polyline: down the right side too
    e.setLayerLines(id, {path({{0.1, 0.25}, {0.9, 0.25}, {0.9, 0.9}})});
    img = capture();
    check(lit(img, 57, 20) && lit(img, 32, 8), "a polyline: each segment");

    // Fill half of it (no rate): its first half lit (from its start)
    e.setLayerLines(id, {path({{0.0, 0.5}, {1.0, 0.5}})});
    input("animation")->lValue = 1;
    input("rate")->fValue = 0;
    input("fill")->fValue = 0.5;
    img = capture();
    check(lit(img, 10, 16) && dark(img, 54, 16), "fill: from its start");
    input("reverse")->bValue = true;
    img = capture();
    check(dark(img, 10, 16) && lit(img, 54, 16), "reversed: from its end");
    input("reverse")->bValue = false;

    // Chase, 2 dashes half wide, still (rate 0): dashes centred at u = 0, 0.5, 1
    input("animation")->lValue = 2;
    input("dashes")->fValue = 2;
    img = capture();
    check(lit(img, 32, 16) && dark(img, 16, 16) && dark(img, 48, 16), "chase: the dashes along it");

    // Offset by line: the second line later — a strobe (duty half) shows one lit, the other dark
    e.setLayerLines(id, {path({{0.0, 0.25}, {1.0, 0.25}}), path({{0.0, 0.75}, {1.0, 0.75}})});
    input("animation")->lValue = 3;
    input("duty")->fValue = 0.5;
    input("cascade")->fValue = 0.25; // rate 0: the first at phase 0 (lit), the second at -0.25 (dark)
    img = capture();
    check(lit(img, 32, 8) != lit(img, 32, 24), "offset: the lines out of step");
    input("cascade")->fValue = 0;
    img = capture();
    check(lit(img, 32, 8) == lit(img, 32, 24), "no offset: in step");

    // Gradient along it
    e.setLayerLines(id, {path({{0.0, 0.5}, {1.0, 0.5}})});
    input("animation")->lValue = 0;
    input("gradient")->bValue = true;
    auto *a = input("lineColor");
    auto *b = input("endColor");
    a->cValue[0] = 1, a->cValue[1] = 0, a->cValue[2] = 0, a->cValue[3] = 1;
    b->cValue[0] = 0, b->cValue[1] = 0, b->cValue[2] = 1, b->cValue[3] = 1;
    img = capture();
    check(qRed(img.pixel(2, 16)) > 200 && qBlue(img.pixel(61, 16)) > 200, "gradient: red at its start, blue at its end");

    // A LED strip on its line: its pixels spread along it, following it when the line or the layer moves
    {
        e.setLayerLines(id, {path({{0.25, 0.5}, {0.75, 0.5}})});
        FixturePatch p;
        DmxOutput o;
        o.id = 1;
        o.enabled = false; // nothing sent on the network
        p.outputs.push_back(o);
        Fixture f;
        f.id = 1;
        f.type = "rgb";
        f.output = 1;
        f.cols = 4;
        f.path = {{0, 0}, {0.1, 0}};
        f.lineLayer = id;
        f.lineIndex = 0;
        p.fixtures.push_back(f);
        e.setFixturePatch(p);
        e.renderFrame();
        Fixture now = e.fixturePatch().fixtures[0];
        check(now.path.size() == 2 && qFuzzyCompare(now.path[0].x(), 0.25) && qFuzzyCompare(now.path[1].x(), 0.75),
              "a strip on a line: its path where the line is");
        const auto pts = fixturePixelPoints(now, *e.fixturePatch().resolvedType(now), QSize(kW, kH));
        check(pts.size() == 4 && qFuzzyCompare(pts[0].x(), 0.25 + 0.0625) && qFuzzyCompare(pts[3].x(), 0.75 - 0.0625),
              "its pixels spread along it");
        e.setLayerLines(id, {path({{0.5, 0.1}, {0.5, 0.9}})});
        e.renderFrame();
        now = e.fixturePatch().fixtures[0];
        check(qFuzzyCompare(now.path[0].x(), 0.5) && qFuzzyCompare(now.path[1].y(), 0.9), "the line moved: the strip follows");
        {
            Engine::Lock lk(&e.mutex());
            e.layer(layer)->mapping.position = QPointF(0.25, 0.5); // the layer moved left by a quarter
            e.layer(layer)->mapping.size = QSizeF(0.5, 1.0);
            ++e.layer(layer)->mapping.revision;
        }
        e.renderFrame();
        now = e.fixturePatch().fixtures[0];
        check(qFuzzyCompare(now.path[0].x(), 0.25), "the layer moved: the strip follows");
        const Fixture back = Fixture::fromJson(now.toJson());
        check(back.lineLayer == id && back.lineIndex == 0 && back.path == now.path, "a strip keeps its path and its line");
        {
            Engine::Lock lk(&e.mutex());
            e.layer(layer)->mapping.position = QPointF(0.5, 0.5);
            e.layer(layer)->mapping.size = QSizeF(1.0, 1.0);
            ++e.layer(layer)->mapping.revision;
        }
        e.setFixturePatch({});
    }

    // A project keeps it, its lines and its values, with its virtual path and no file to find
    QTemporaryDir tmp;
    const QString file = tmp.path() + "/lines.pinol";
    e.setLayerLines(id, {path({{0.1, 0.1}, {0.9, 0.2}, {0.5, 0.8}}, true)});
    input("thickness")->fValue = 5;
    const QJsonObject source = e.layerJson(layer).value("source").toObject();
    check(isLineGeneratorPath(source.value("path").toString()) && !source.contains("relative_path") &&
              source.value("lines").toArray().size() == 1,
          "saved with its virtual path and its lines");
    check(e.saveProject(file, {}, &err), "the project is saved");
    e.newProject();
    check(e.loadProject(file, nullptr, &err), "the project loads");
    int loaded = -1;
    for (int i = 0; i < e.layerCount(); ++i) {
        Engine::Lock lk(&e.mutex());
        const Layer *l = e.layer(i);
        if (l->type != SourceType::Isf || !isLineGeneratorPath(l->sourcePath)) continue;
        loaded = i;
        check(l->generator && l->generator->isValid() && l->error.isEmpty(), "loaded, valid, no error");
        check(l->lines.size() == 1 && l->lines[0].closed && l->lines[0].points.size() == 3, "its lines back");
        const IsfInput *t = l->generator ? l->generator->input(QStringLiteral("thickness")) : nullptr;
        check(t && qFuzzyCompare(t->fValue, 5.0), "its values kept");
    }
    check(loaded >= 0, "the Line layer is back");
    check(e.planCollect(tmp.path(), "Collected", {}).missing.isEmpty(), "Collect Files finds no missing file");

    // Another generator in its place: its lines go
    if (loaded >= 0) {
        e.setLayerIsf(loaded, QStringLiteral(PINOL_SOURCE_DIR "/isf/generators/SolidColor.fs"), &err);
        Engine::Lock lk(&e.mutex());
        check(e.layer(loaded)->lines.empty(), "another generator: no lines");
    }

    e.shutdown();
    std::printf("line_generator: %s (%d failure%s)\n", failures ? "FAIL" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
