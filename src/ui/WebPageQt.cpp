// The Web Page generator's browser (WebPageQt.h): Qt WebEngine.
//
// How a page becomes pictures: a QWebEngineView of the page's size, shown off screen (Qt::WA_DontShowOnScreen: Qt and
// Chromium take it for visible, so the page runs and paints as in a window, but nothing appears on a screen). Qt
// WebEngine draws it with a QQuickWidget, whose picture is read back (QQuickWidget::grabFramebuffer, alpha kept) at
// the page's frame rate, only when the page has painted since (its window's afterRendering). Other Qt versions, whose
// view would not be a QQuickWidget, fall back to QWidget::grab (every tick). The picture is turned bottom row first
// and RGBA here, on the application's thread; the render thread only uploads it (LiveInput::update).
//
// The cost is a GPU read back and a copy per new picture (a 1920 × 1080 page: 8 MB, a few milliseconds of the
// application's thread at 30 fps). Rendering through QQuickRenderControl into a texture shared with the engine would
// save them, but needs Qt Quick's private plumbing that differs between the Qt versions Pinol is built with.
//
// The engine creates, changes and destroys its pages from any thread with its lock held: everything here is passed
// to the application's thread (queued to the application object), and each page's state shared with a mutex.
#include "WebPageQt.h"

#ifdef PINOL_HAS_WEB

#include "AnnotatedMutex.h"
#include "Gl.h"
#include "WebPage.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <atomic>
#include <cmath>
#include <set>

namespace {

class WebPageHost;

// What a page and its host share: the newest picture, the status, and the host (application thread only)
struct WebShared {
    Annotated<QMutex> mutex;
    QImage frame; // RGBA, bottom row first
    bool fresh = false;
    QString status = QStringLiteral("Loading…");
    QPointer<WebPageHost> host; // the application's thread only
    std::atomic<bool> closed{false}; // its page is gone (a host not made yet is not made)
};

// Runs `f` on the application's thread, later (never under the caller's locks)
template <class F> void post(F &&f)
{
    if (QCoreApplication *app = QCoreApplication::instance()) QMetaObject::invokeMethod(app, std::forward<F>(f), Qt::QueuedConnection);
}

QUrl pageUrl(const QString &url)
{
    if (url.trimmed().isEmpty()) return QUrl(QStringLiteral("about:blank"));
    return QUrl::fromUserInput(url.trimmed());
}

// The hosts alive: closed with the application (the pages must go before Qt WebEngine's profile)
std::set<WebPageHost *> &hosts()
{
    static std::set<WebPageHost *> h;
    return h;
}

// One page: its view, off screen, and its clock. The application's thread.
class WebPageHost : public QObject
{
public:
    WebPageHost(std::shared_ptr<WebShared> shared, const WebSource &s) : m_shared(std::move(shared))
    {
        hosts().insert(this);
        m_view = new QWebEngineView;
        m_view->setAttribute(Qt::WA_DontShowOnScreen);
        m_view->setAttribute(Qt::WA_TranslucentBackground); // its picture keeps the page's transparency
        m_view->setAttribute(Qt::WA_ShowWithoutActivating);
        m_view->setFocusPolicy(Qt::NoFocus);
        QWebEnginePage *page = m_view->page();
        page->setAudioMuted(true); // a page's sound is not part of the show
        QWebEngineSettings *settings = page->settings();
        settings->setAttribute(QWebEngineSettings::ShowScrollBars, false);
        settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false); // its videos play by themselves
        settings->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, false);
        QObject::connect(page, &QWebEnginePage::loadStarted, this, [this] { setStatus(QStringLiteral("Loading…")); });
        QObject::connect(page, &QWebEnginePage::loadProgress, this, [this](int p) {
            if (p < 100) setStatus(QStringLiteral("Loading… %1%").arg(p));
        });
        QObject::connect(page, &QWebEnginePage::loadFinished, this, [this](bool ok) {
            setStatus(ok ? QString() : QStringLiteral("The page could not be loaded: %1").arg(m_url.toDisplayString()));
            m_dirty = true;
        });
        QObject::connect(page, &QWebEnginePage::renderProcessTerminated, this,
                         [this](QWebEnginePage::RenderProcessTerminationStatus status, int code) {
                             if (status == QWebEnginePage::NormalTerminationStatus) return;
                             setStatus(QStringLiteral("The page's renderer stopped (code %1): reload it").arg(code));
                         });
        m_timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] { take(); });
        apply(s, true);
        m_view->show(); // off screen
    }
    ~WebPageHost() override
    {
        hosts().erase(this);
        delete m_view;
    }

    void apply(const WebSource &s, bool first = false)
    {
        if (!first && s == m_settings) return;
        const WebSource was = m_settings;
        m_settings = s;
        if (first || s.width != was.width || s.height != was.height) m_view->resize(s.width, s.height);
        if (first || s.transparent != was.transparent)
            m_view->page()->setBackgroundColor(s.transparent ? QColor(Qt::transparent) : QColor(Qt::white));
        if (first || s.zoom != was.zoom) m_view->page()->setZoomFactor(s.zoom);
        if (first || s.fps != was.fps) m_timer.start(std::max(1, int(std::lround(1000.0 / s.fps))));
        if (first || s.url != was.url) {
            m_url = pageUrl(s.url);
            m_view->load(m_url);
        }
        m_dirty = true;
    }
    void reload()
    {
        if (m_view->url().isEmpty() || m_view->url() != m_url) m_view->load(m_url); // never shown: loaded again
        else m_view->page()->triggerAction(QWebEnginePage::ReloadAndBypassCache);
    }

private:
    void setStatus(const QString &s)
    {
        QMutexLocker lk(&m_shared->mutex);
        m_shared->status = s;
    }
    // A picture of the page, when it has painted since the last one
    void take()
    {
        auto *quick = qobject_cast<QQuickWidget *>(m_view->focusProxy());
        if (quick && quick != m_tracked) { // the widget Qt WebEngine draws in (made when the page is): its paints followed
            m_tracked = quick;
            if (QQuickWindow *w = quick->quickWindow())
                QObject::connect(w, &QQuickWindow::afterRendering, this, [this] { m_dirty = true; }, Qt::DirectConnection);
            m_dirty = true;
        }
        if (quick && !m_dirty) return;
        QImage img = quick ? quick->grabFramebuffer() : m_view->grab().toImage();
        m_dirty = false; // after the grab: what it rendered itself is in this picture
        if (img.isNull()) return;
        QImage rgba = flippedVertically(img.convertToFormat(QImage::Format_RGBA8888));
        QMutexLocker lk(&m_shared->mutex);
        m_shared->frame = std::move(rgba);
        m_shared->fresh = true;
    }

    std::shared_ptr<WebShared> m_shared;
    QWebEngineView *m_view = nullptr;
    QPointer<QQuickWidget> m_tracked;
    QTimer m_timer;
    WebSource m_settings;
    QUrl m_url;
    std::atomic<bool> m_dirty{true}; // set where the page renders (the same thread, but in Qt Quick's code)
};

// The engine's side: any thread
class QtWebPage : public WebPage
{
public:
    explicit QtWebPage(const WebSource &s) : m_shared(std::make_shared<WebShared>()), m_settings(s)
    {
        post([shared = m_shared, s] {
            if (!shared->closed) shared->host = new WebPageHost(shared, s);
        });
    }
    ~QtWebPage() override
    {
        m_shared->closed = true;
        post([shared = m_shared] { delete shared->host.data(); });
    }
    bool update(Texture2D &tex) override
    {
        QImage frame;
        {
            QMutexLocker lk(&m_shared->mutex);
            if (!m_shared->fresh) return false;
            m_shared->fresh = false;
            frame = m_shared->frame; // shared, not copied
        }
        tex.upload(frame.constBits(), frame.width(), frame.height());
        return true;
    }
    QString status() const override
    {
        QMutexLocker lk(&m_shared->mutex);
        return m_shared->status;
    }
    void configure(const WebSource &s) override
    {
        if (s == m_settings) return;
        m_settings = s;
        post([shared = m_shared, s] {
            if (shared->host) shared->host->apply(s);
        });
    }
    void reload() override
    {
        post([shared = m_shared] {
            if (shared->host) shared->host->reload();
        });
    }

private:
    std::shared_ptr<WebShared> m_shared;
    WebSource m_settings; // as last given (the engine's lock held)
};

} // namespace

bool registerWebPages()
{
    if (!qobject_cast<QApplication *>(QCoreApplication::instance())) return false; // Qt WebEngine's view is a widget
    // Pages left when the application quits go first, while Qt WebEngine is still there
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] {
        while (!hosts().empty()) delete *hosts().begin();
    });
    setWebPageFactory([](const WebSource &s) -> std::unique_ptr<WebPage> { return std::make_unique<QtWebPage>(s); });
    return true;
}

#else

bool registerWebPages() { return false; }

#endif
