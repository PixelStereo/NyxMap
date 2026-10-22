#pragma once
// The Web Page generator: a layer's source that is a web page, rendered by a browser at a size and a pace of its own
// (SourceType::Web). Its settings are plain data (WebSource), stored with the layer:
//   "source": {"type": "web", "url", "width", "height", "fps", "transparent", "zoom"}
// and its parameters (web/url, web/reload…: LayerParams.cpp), so snapshots, presets and OSC reach them.
//
// The engine has no browser of its own (it does not depend on widgets): the application registers one at startup
// (setWebPageFactory; Pinol's, with Qt WebEngine, is src/ui/WebPageQt.cpp). Without one, a project's web layers are
// kept with an error, and the Media Bin does not offer the generator.

#include "LiveInput.h"

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <functional>
#include <memory>

struct WebSource {
    QString url;                 // what it shows (empty: a blank page)
    int width = 1920, height = 1080; // the size of its picture, in pixels (the composition's by default)
    double fps = 30;             // the pictures taken of it per second
    bool transparent = false;    // the page's background left transparent (where the page itself does not paint one)
    double zoom = 1.0;           // the page's zoom factor

    // Values kept in their useful range (a project, a snapshot or OSC may give anything)
    void sanitize();
    QJsonObject toJson() const; // its fields, without "type"
    // Missing fields take their default; width and height `size`'s (the composition's) when given
    static WebSource fromJson(const QJsonObject &o, const QSize &size = QSize());
    bool operator==(const WebSource &o) const
    {
        return url == o.url && width == o.width && height == o.height && fps == o.fps && transparent == o.transparent &&
               zoom == o.zoom;
    }
    bool operator!=(const WebSource &o) const { return !(*this == o); }

    static constexpr int kMaxSize = 8192;
    static constexpr double kMinFps = 1, kMaxFps = 60, kMinZoom = 0.25, kMaxZoom = 5;
};

// A page being shown. Its pictures come in on its own thread (the application's); the render thread takes the newest
// one (LiveInput::update). Created, changed and destroyed from any thread, the engine's lock held: it must not block.
class WebPage : public LiveInput
{
public:
    virtual void configure(const WebSource &s) = 0; // what it shows and how (the page is loaded again only for a new URL)
    virtual void reload() = 0;
};

using WebPageFactory = std::function<std::unique_ptr<WebPage>(const WebSource &)>;
void setWebPageFactory(WebPageFactory factory); // the application, at startup (empty: none)
bool webPagesAvailable();
// Null with `err` when there is no browser in this build
std::unique_ptr<WebPage> createWebPage(const WebSource &s, QString *err = nullptr);
