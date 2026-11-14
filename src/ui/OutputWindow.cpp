#include "OutputWindow.h"
#include "Engine.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QSurfaceFormat>

#ifdef Q_OS_MACOS
#include "MacPresentation.h"
#endif

OutputWindow::OutputWindow(Engine *engine, quint64 viewport) : m_engine(engine), m_viewport(viewport)
{
    // macOS: shown through Metal by the render thread; elsewhere through OpenGL
    setSurfaceType(Engine::metalOutput() ? QSurface::MetalSurface : QSurface::OpenGLSurface);
    if (surfaceType() == QSurface::OpenGLSurface) setFormat(QSurfaceFormat::defaultFormat());
    setTitle(QStringLiteral("NyxMap — Output"));
    create();
    m_engine->setViewportWindow(m_viewport, this);
    // Moved to another screen: its refresh rate is the render's pace from now on
    connect(this, &QWindow::screenChanged, this, [this] { sync(); });
}

OutputWindow::~OutputWindow()
{
    if (m_viewport) m_engine->setViewportWindow(m_viewport, nullptr);
}

void OutputWindow::sync()
{
    if (!m_viewport) return; // disposed
    const bool exposed = isVisible() && isExposed();
    const QSize px = size() * devicePixelRatio();
    const double hz = screen() ? screen()->refreshRate() : 0;
    if (exposed == m_lastExposed && px == m_lastSize && hz == m_lastHz) return;
    m_lastExposed = exposed;
    m_lastSize = px;
    m_lastHz = hz;
    m_engine->setViewportExposed(m_viewport, exposed, px, hz);
}

// Fullscreen is instant everywhere: on macOS a borderless window covering its screen above the menu bar
// and the Dock (no Space of its own, so no animation to wait for), elsewhere showFullScreen.
void OutputWindow::showOn(QScreen *screen, bool fullscreen)
{
    if (!screen) screen = QGuiApplication::primaryScreen();
    hideNow();
    setScreen(screen);
    if (fullscreen) {
        setCursor(Qt::BlankCursor);
#ifdef Q_OS_MACOS
        setFlags(flags() | Qt::FramelessWindowHint);
        setGeometry(screen->geometry());
        show();
        macSetCoversScreen(this, true); // once the native window exists and is shown
        raise();
        requestActivate();
        m_coversScreen = true;
#else
        setGeometry(screen->geometry());
        showFullScreen();
#endif
    } else {
        const QRect g = screen->availableGeometry();
        const QSize s(qMin(1280, g.width() * 2 / 3), qMin(720, g.height() * 2 / 3));
        setGeometry(QRect(g.center() - QPoint(s.width() / 2, s.height() / 2), s));
        unsetCursor();
        showNormal();
    }
}

void OutputWindow::hideOutput()
{
    hideNow();
}

void OutputWindow::dispose()
{
    m_engine->setViewportWindow(m_viewport, nullptr);
    m_viewport = 0;
    deleteLater();
}

QScreen *OutputWindow::screenNamed(const QString &name)
{
    const auto screens = QGuiApplication::screens();
    for (QScreen *sc : screens)
        if (sc->name() == name) return sc;
    QScreen *primary = QGuiApplication::primaryScreen();
    for (QScreen *sc : screens)
        if (sc != primary) return sc;
    return primary;
}

void OutputWindow::hideNow()
{
    // The render thread stops using the window before it is hidden.
    if (m_lastExposed) {
        m_lastExposed = false;
        m_engine->setViewportExposed(m_viewport, false, m_lastSize);
    }
    hide();
#ifdef Q_OS_MACOS
    if (m_coversScreen) { // back to a framed window at the normal level, for a later windowed show
        m_coversScreen = false;
        macSetCoversScreen(this, false);
        setFlags(flags() & ~Qt::FramelessWindowHint);
    }
#endif
}

void OutputWindow::exposeEvent(QExposeEvent *e)
{
    QWindow::exposeEvent(e);
    sync();
}

void OutputWindow::resizeEvent(QResizeEvent *e)
{
    QWindow::resizeEvent(e);
    sync();
}

bool OutputWindow::event(QEvent *e)
{
    if (e->type() == QEvent::Close) {
        // Closing the window hides the output (handled by the main window)
        emit closeRequested();
        e->ignore();
        return true;
    }
    if (e->type() == QEvent::Hide || e->type() == QEvent::Show) {
        const bool r = QWindow::event(e);
        sync();
        return r;
    }
    return QWindow::event(e);
}

void OutputWindow::keyPressEvent(QKeyEvent *e)
{
    // Esc does not close the output (safety during a show): Shift+Esc is required.
    if (e->key() == Qt::Key_Escape && (e->modifiers() & Qt::ShiftModifier)) {
        emit closeRequested();
        return;
    }
    emit keyPressed(e->key(), e->modifiers());
    QWindow::keyPressEvent(e);
}
