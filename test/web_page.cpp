// The Web Page generator (WebPage.h): its settings saved and loaded, its parameters (URL, reload…) as snapshots and
// OSC reach them, with a browser of the test's own (a solid color). Then, when NyxMap is built with Qt WebEngine
// (NYXMAP_HAS_WEB), a real page: a local HTML file of one color, loaded, and its picture read back in the layer.
#include "Engine.h"
#include "WebPage.h"
#include "WebPageQt.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <cstdio>

namespace {

int failures = 0;
void check(bool ok, const char *what)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL web_page: %s\n", what);
    }
}

// The test's browser: a page of one color, that says what it was given
struct FakeLog {
    std::atomic<int> reloads{0}, configures{0};
    QString url; // the last one given (the engine's lock held)
};
FakeLog fakeLog;

class FakePage : public WebPage
{
public:
    explicit FakePage(const WebSource &s) : m_settings(s) { fakeLog.url = s.url; }
    bool update(Texture2D &tex) override
    {
        if (m_sent) return false;
        m_sent = true;
        QImage img(m_settings.width, m_settings.height, QImage::Format_RGBA8888);
        img.fill(QColor(0, 0, 255));
        tex.upload(img.constBits(), img.width(), img.height());
        return true;
    }
    void configure(const WebSource &s) override
    {
        m_settings = s;
        fakeLog.url = s.url;
        ++fakeLog.configures;
    }
    void reload() override { ++fakeLog.reloads; }

private:
    WebSource m_settings;
    bool m_sent = false;
};

QColor centre(const QImage &img) { return img.isNull() ? QColor() : img.pixelColor(img.width() / 2, img.height() / 2); }

} // namespace

int main(int argc, char **argv)
{
#ifdef NYXMAP_HAS_WEB
    // Chromium in a CI container: no sandbox (no user namespaces there) and no GPU. Here only: the application keeps
    // Chromium's sandbox
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--no-sandbox --disable-gpu");
    qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
#endif
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    Engine e;
    QString err;
    if (!e.initialize(&err)) {
        std::printf("FAIL web_page: OpenGL: %s\n", qPrintable(err));
        return 1;
    }
    QTemporaryDir tmp;
    e.newProject();
    e.fadeCompositionOpacity(1.0, 0);
    e.setBlackout(false, 0);
    e.setCompositionSize(QSize(64, 36));

    // --- Without a browser: refused, or kept with its error (a project made elsewhere)
    setWebPageFactory({});
    check(!webPagesAvailable(), "no browser registered");
    int li = e.addLayer("Web");
    const quint64 id = e.layerId(li);
    check(!e.setLayerWeb(li, WebSource{}, &err) && !err.isEmpty(), "refused without a browser, with why");
    check(e.setLayerWeb(li, WebSource{}, &err, true) && !err.isEmpty(), "kept without a browser");
    {
        Engine::Lock lk(&e.mutex());
        const Layer *l = e.layer(e.indexOfId(id));
        check(l->type == SourceType::Web && !l->webPage && !l->error.isEmpty(), "a web layer with its error");
    }

    // --- With the test's browser: the settings, the parameters, the picture
    setWebPageFactory([](const WebSource &s) -> std::unique_ptr<WebPage> { return std::make_unique<FakePage>(s); });
    check(webPagesAvailable(), "browser registered");
    WebSource s;
    s.url = QStringLiteral("https://example.org/show");
    s.width = 320;
    s.height = 180;
    s.fps = 25;
    s.transparent = true;
    s.zoom = 1.5;
    li = e.indexOfId(id);
    check(e.setLayerWeb(li, s, &err) && err.isEmpty(), "web page set");
    QJsonObject src = e.layerJson(li).value("source").toObject();
    check(src.value("type") == "web" && src.value("url") == s.url && src.value("width") == 320 &&
              src.value("height") == 180 && src.value("fps") == 25.0 && src.value("transparent") == true &&
              src.value("zoom") == 1.5,
          "source JSON {type: web, url, width, height, fps, transparent, zoom}");
    check(WebSource::fromJson(src) == s, "JSON round trip of the settings");
    check(WebSource::fromJson(QJsonObject{{"fps", 1000}, {"zoom", -3}, {"width", 0}}, QSize(1280, 720)).fps == WebSource::kMaxFps &&
              WebSource::fromJson(QJsonObject{{"zoom", -3}}).zoom == WebSource::kMinZoom &&
              WebSource::fromJson(QJsonObject{}, QSize(1280, 720)).width == 1280 &&
              WebSource::fromJson(QJsonObject{{"width", 0}}).width == 1,
          "settings kept in range, composition size by default");

    ParamInfo info;
    check(e.parameterInfo(id, "web/url", &info) && info.type == ParamInfo::Type::Text, "web/url: a text parameter");
    check(e.parameterInfo(id, "web/reload", &info) && info.type == ParamInfo::Type::Trigger, "web/reload: a trigger");
    check(e.parameterInfo(id, "web/width", &info) && info.defaultValue.toInt() == 64, "web/width: composition's by default");
    check(e.parameterValue(id, "web/url") == QJsonValue(s.url), "web/url read");
    check(e.setParameter(id, "web/url", QStringLiteral("https://nyxmap.fr")) && fakeLog.url == "https://nyxmap.fr",
          "web/url written: the page is given it");
    check(e.setParameter(id, "web/reload", true) && fakeLog.reloads == 1, "web/reload: the page reloads");
    check(e.setParameter(id, "web/zoom", 2.0) && e.parameterValue(id, "web/zoom").toDouble() == 2.0, "web/zoom written");
    check(e.setParameter(id, "web/width", 400) && e.layerJson(e.indexOfId(id)).value("source").toObject().value("width") == 400,
          "web/width written, in the source");

    for (int k = 0; k < 3; ++k) e.renderFrame();
    QColor c = centre(e.grabLayerSource(e.indexOfId(id)));
    check(c.blue() > 250 && c.red() < 5 && c.green() < 5, "the page's picture in the layer");

    // A snapshot holds its URL: recalled, the page is given it back
    const QJsonArray held = e.captureLayers();
    e.setParameter(id, "web/url", QStringLiteral("https://elsewhere.example"));
    e.applyLayers(held, 0.0);
    check(e.parameterValue(id, "web/url") == QJsonValue("https://nyxmap.fr") && fakeLog.url == "https://nyxmap.fr",
          "a snapshot recalls the URL");

    // Saved and loaded
    const QJsonObject before = e.layerJson(e.indexOfId(id)).value("source").toObject();
    check(e.saveProject(tmp.filePath("web.nyxmap"), {}, &err), "project saved");
    e.newProject();
    check(e.loadProject(tmp.filePath("web.nyxmap"), nullptr, &err), "project loaded");
    check(e.indexOfId(id) >= 0 && e.layerJson(e.indexOfId(id)).value("source").toObject() == before, "web source loaded back");
    check(e.parameterValue(id, "web/url") == QJsonValue("https://nyxmap.fr") && fakeLog.url == "https://nyxmap.fr",
          "loaded page shows its URL");
    e.newProject();

#ifdef NYXMAP_HAS_WEB
    // --- A real page, with Qt WebEngine: a local file of one color
    check(registerWebPages() && webPagesAvailable(), "Qt WebEngine registered");
    {
        QFile html(tmp.filePath("red.html"));
        check(html.open(QIODevice::WriteOnly), "HTML fixture opened");
        html.write("<!doctype html><html><body style=\"margin:0;background:rgb(255,0,0)\"></body></html>");
        html.close();
        e.fadeCompositionOpacity(1.0, 0);
        e.setBlackout(false, 0);
        e.setCompositionSize(QSize(64, 36));
        const quint64 wid = e.layerId(e.addLayer("Red page"));
        WebSource page;
        page.url = QDir::cleanPath(tmp.filePath("red.html")); // a path: QUrl::fromUserInput makes a file:// URL of it
        page.width = 160;
        page.height = 90;
        check(e.setLayerWeb(e.indexOfId(wid), page, &err), "Qt WebEngine page set");
        QElapsedTimer wait;
        wait.start();
        QColor got;
        while (wait.elapsed() < 60000) { // Chromium starts its processes the first time
            app.processEvents(QEventLoop::AllEvents, 50);
            e.renderFrame();
            got = centre(e.grabLayerSource(e.indexOfId(wid)));
            if (got.red() > 240 && got.green() < 15 && got.blue() < 15) break;
            QThread::msleep(20);
        }
        if (!(got.red() > 240 && got.green() < 15 && got.blue() < 15))
            std::printf("web_page: centre %s after %lld ms, status \"%s\"\n", qPrintable(got.name(QColor::HexArgb)),
                        static_cast<long long>(wait.elapsed()), qPrintable(e.layerWebStatus(e.indexOfId(wid))));
        check(got.red() > 240 && got.green() < 15 && got.blue() < 15, "the red page's picture in the layer");
        e.newProject();
        // Its view goes on the application's thread
        for (int k = 0; k < 20; ++k) app.processEvents(QEventLoop::AllEvents, 10);
    }
#else
    std::printf("web_page: built without Qt WebEngine, real page skipped\n");
#endif
    setWebPageFactory({});
    e.shutdown();
    if (!failures) std::printf("web_page: ok\n");
    return failures ? 1 : 0;
}
