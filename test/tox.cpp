// Host behavior with a deterministic component. Native SDK compilation is covered
// by Windows/macOS CI; licensed TouchDesigner playback needs a real installation.
#include "Engine.h"
#include "Tox.h"
#include <QFile>
#include <QImage>
#include <cstdio>
#include <limits>

namespace {
struct FakeTox final : ToxPlayer {
    ToxSettings settings;
    QMap<QString, GLuint> textures;
    int reloads = 0;
    explicit FakeTox(const ToxSettings &s) : settings(s) {}
    void configure(const ToxSettings &s) override { settings = s; }
    void reload() override { ++reloads; }
    void pulse(const QString &) override {}
    std::vector<ToxControl> controls() const override {
        ToxControl c;
        c.id = "color/rgba"; c.label = "Tint"; c.count = 4; c.color = true;
        c.initial = QJsonArray{1.0, 0.0, 0.0, 1.0}; c.minimum = {0, 0, 0, 0}; c.maximum = {1, 1, 1, 1};
        return {c};
    }
    std::vector<std::pair<QString, QString>> inputs() const override { return {{"in1", "Image A"}, {"in2", "Image B"}}; }
    std::vector<std::pair<QString, QString>> outputs() const override { return {{"out1", "Result"}, {"out2", "Preview"}}; }
    quint64 revision() const override { return 1; }
    QString error() const override { return {}; }
    void setInputTexture(const QString &id, GLuint texture, int, int) override { textures[id] = texture; }
    bool update(Texture2D &target) override {
        QImage image(16, 16, QImage::Format_RGBA8888); image.fill(Qt::green);
        target.upload(image.constBits(), 16, 16); return true;
    }
};
}

int runToxTests(Engine &e, const QString &tmp)
{
    int failures = 0;
    auto check = [&](bool ok, const char *message) { std::printf("%s tox: %s\n", ok ? "ok  " : "FAIL", message); if (!ok) ++failures; };
    e.newProject();
    e.setCompositionSize(QSize(32, 32));
    ToxSettings settings;
    settings.fps = std::numeric_limits<double>::infinity(); settings.sanitize();
    check(settings.fps == 30, "invalid frame rate bounded");
    settings.output = "out2"; settings.inputs["in1"] = 9007199254740993ULL;
    settings.values["color/rgba"] = QJsonArray{.1, .2, .3, 1.0};
    check(ToxSettings::fromJson(settings.toJson()).toJson() == settings.toJson(), "stable IDs and vectors survive JSON without precision loss");
    settings.inputs.clear();
    const QString component = tmp + "/sample.tox";
    { QFile f(component); check(f.open(QIODevice::WriteOnly) && f.write("fake") == 4, "component fixture"); }
    setToxFactoryForTests([](const QString &, const ToxSettings &s) { return std::make_unique<FakeTox>(s); });
    const int a = e.addLayer("Image A"); const quint64 aid = e.layerId(a);
    const int b = e.addLayer("Image B"); const quint64 bid = e.layerId(b);
    QImage red(16, 16, QImage::Format_RGBA8888); red.fill(Qt::red);
    const QString image = tmp + "/tox-red.png";
    check(red.save(image), "input fixture");
    QString error;
    check(e.setLayerImage(e.indexOfId(aid), image, &error) && e.setLayerImage(e.indexOfId(bid), image, &error), "input images loaded");
    const int tox = e.addLayer("Component"); const quint64 id = e.layerId(tox);
    check(e.setLayerFile(tox, component, &error), ".tox auto-detected");
    check(e.setLayerToxInput(tox, "in1", aid, &error) && e.setLayerToxInput(tox, "in2", bid, &error), "independent input TOPs");
    check(!e.setLayerToxInput(tox, "in1", id, &error), "self-connection rejected");
    check(!e.setLayerToxInput(tox, "in1", e.mainViewportId(), &error), "viewport rejected");
    check(!e.setLayerToxInput(tox, "in1", 9999999, &error), "missing layer rejected");
    check(!e.setLayerToxInput(tox, "unknown", aid, &error), "unknown TOP rejected");
    check(!e.setLayerSourceLayer(e.indexOfId(aid), id, LayerTap::PostFx, &error), "reverse dependency cycle rejected");
    const ToxControl c = e.layer(tox)->toxPlayer->controls().front();
    ParamInfo info;
    check(e.parameterInfo(id, c.path(0), &info) && info.colorComponent == 0 && info.animatable, "RGBA metadata exposes combined color animation");
    check(e.setParameter(id, c.path(0), .6) && e.setParameter(id, "tox/output", "out2"), "exposed controls and output selection writable");
    e.layer(e.indexOfId(aid))->enabled = false;
    e.renderFrame();
    auto *fake = dynamic_cast<FakeTox *>(e.layer(e.indexOfId(id))->toxPlayer.get());
    check(fake && fake->textures.value("in1") && fake->textures.value("in2") &&
          fake->textures.value("in1") == e.layer(e.indexOfId(aid))->finalTex &&
          fake->textures.value("in2") == e.layer(e.indexOfId(bid))->finalTex, "hidden dependencies rendered before TOP input delivery");
    Engine::Snapshot snap;
    snap.name = "TOP routing"; snap.fade = 0; snap.layers = e.captureLayers();
    const int snapshot = e.addSnapshot(snap);
    check(e.setLayerToxInput(e.indexOfId(id), "in1", 0) && e.setParameter(id, c.path(0), .1), "change routing and tint after snapshot");
    e.recallSnapshot(snapshot); e.advanceFades(.01);
    check(e.layer(e.indexOfId(id))->tox.inputs.value("in1") == aid && std::abs(e.parameterValue(id, c.path(0)).toDouble() - .6) < 1e-6,
          "snapshot restores TOP routing and values");
    const QString project = tmp + "/tox.pinol";
    check(e.saveProject(project, {}, &error), "save component project");
    e.newProject();
    check(e.loadProject(project, nullptr, &error), "restore component project");
    const int restored = e.indexOfId(id);
    check(restored >= 0 && e.layer(restored)->type == SourceType::Tox && e.layer(restored)->tox.inputs.value("in1") == aid &&
          e.layer(restored)->tox.inputs.value("in2") == bid && e.layer(restored)->tox.output == "out2", "routing and output survive reopen");
    check(std::abs(e.parameterValue(id, c.path(0)).toDouble() - .6) < 1e-6, "parameter values survive reopen");
    e.removeLayer(e.indexOfId(aid));
    check(e.layer(e.indexOfId(id))->tox.inputs.value("in1") == 0 && e.layer(e.indexOfId(id))->tox.inputs.value("in2") == bid,
          "deleting one source disconnects only its TOP");
    QJsonObject cyclic = e.layerJson(e.indexOfId(id));
    QJsonObject src = cyclic["source"].toObject();
    src["inputs"] = QJsonObject{{"in1", QString::number(id)}, {"in2", QString::number(bid)}}; cyclic["source"] = src;
    e.replaceLayerById(id, cyclic);
    check(e.layer(e.indexOfId(id))->tox.inputs.value("in1") == 0, "invalid saved routing repaired");
    const QString relinked = tmp + "/relinked.tox";
    check(QFile::copy(component, relinked) && e.relinkLayerMedia(e.indexOfId(id), component, relinked, &error), "relink component");
    check(e.layer(e.indexOfId(id))->tox.inputs.value("in2") == bid && e.layer(e.indexOfId(id))->tox.output == "out2", "relink preserves TOP settings");
    bool inBin = false;
    for (const auto &r : e.mediaUsage()) if (r.path == relinked) inBin = r.video && !r.missing;
    check(inBin, "component is a real managed media file");
    e.newProject();
    setToxFactoryForTests({});
    if (!touchEngineCompiledIn()) {
        const int unsupported = e.addLayer("Unsupported");
        check(e.setLayerTox(unsupported, component, settings, &error) && !e.layer(unsupported)->toxPlayer->error().isEmpty(),
              "unsupported builds keep source and settings with an actionable error");
    }
    e.newProject();
    return failures;
}
