// The Web Page generator's settings, and the browser the application gives the engine (WebPage.h)
#include "WebPage.h"
#include "AnnotatedMutex.h"

#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>

namespace {

Annotated<QMutex> &factoryMutex()
{
    static Annotated<QMutex> m;
    return m;
}

WebPageFactory &factory()
{
    static WebPageFactory f;
    return f;
}

} // namespace

void WebSource::sanitize()
{
    url = url.trimmed();
    width = std::clamp(width, 1, kMaxSize);
    height = std::clamp(height, 1, kMaxSize);
    fps = std::isfinite(fps) ? std::clamp(fps, kMinFps, kMaxFps) : 30.0;
    zoom = std::isfinite(zoom) ? std::clamp(zoom, kMinZoom, kMaxZoom) : 1.0;
}

QJsonObject WebSource::toJson() const
{
    return {{"url", url}, {"width", width}, {"height", height}, {"fps", fps}, {"transparent", transparent}, {"zoom", zoom}};
}

WebSource WebSource::fromJson(const QJsonObject &o, const QSize &size)
{
    WebSource s;
    if (size.isValid()) {
        s.width = size.width();
        s.height = size.height();
    }
    s.url = o.value("url").toString();
    s.width = o.value("width").toInt(s.width);
    s.height = o.value("height").toInt(s.height);
    s.fps = o.value("fps").toDouble(s.fps);
    s.transparent = o.value("transparent").toBool(s.transparent);
    s.zoom = o.value("zoom").toDouble(s.zoom);
    s.sanitize();
    return s;
}

void setWebPageFactory(WebPageFactory f)
{
    QMutexLocker lk(&factoryMutex());
    factory() = std::move(f);
}

bool webPagesAvailable()
{
    QMutexLocker lk(&factoryMutex());
    return bool(factory());
}

std::unique_ptr<WebPage> createWebPage(const WebSource &s, QString *err)
{
    WebPageFactory f;
    {
        QMutexLocker lk(&factoryMutex());
        f = factory();
    }
    std::unique_ptr<WebPage> page = f ? f(s) : nullptr;
    if (!page && err) *err = QStringLiteral("Web pages are not available in this build of Pinol (built without Qt WebEngine)");
    return page;
}
