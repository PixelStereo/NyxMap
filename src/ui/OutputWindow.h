#pragma once
#include <QWindow>

class Engine;
class QScreen;

// Output window of one viewport, for its screen or projector. It draws nothing itself:
// the engine's render thread presents the viewport's picture into it, locked to vsync,
// independently of the UI.
class OutputWindow : public QWindow
{
    Q_OBJECT
public:
    OutputWindow(Engine *engine, quint64 viewport);
    quint64 viewport() const { return m_viewport; }
    ~OutputWindow() override;

    // Fullscreen on the chosen screen, or a window. Instant on every platform: on macOS the window covers
    // its screen above the menu bar and the Dock instead of opening a Space of its own.
    void showOn(QScreen *screen, bool fullscreen);
    void hideOutput();
    // Lets go of the viewport at once and deletes the window
    void dispose();

    // The screen of that name; without one (or once unplugged), the first screen that is not the main
    // screen (a projector), otherwise the main screen.
    static QScreen *screenNamed(const QString &name);

signals:
    void closeRequested();
    void keyPressed(int key, Qt::KeyboardModifiers modifiers); // forwarded to the main window

protected:
    void exposeEvent(QExposeEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool event(QEvent *e) override;

private:
    void sync();
    void hideNow();
    Engine *m_engine;
    quint64 m_viewport = 0;
    bool m_lastExposed = false;
    bool m_coversScreen = false; // macOS: borderless above the menu bar and the Dock
    QSize m_lastSize;
    double m_lastHz = 0; // its screen's refresh rate, as last told to the engine
};
