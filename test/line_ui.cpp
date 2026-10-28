// The Line generator's lines in the Mapping view: drawn point by point (ended by Enter, a double-click, or its first
// point again: closed), a point dragged, a point added on a segment, a point and a line deleted — each one undoable.
#include "Engine.h"
#include "MappingView.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QUndoStack>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setApplicationName("NyxMapLineUITest");
    Engine engine;
    QString error;
    if (!engine.initialize(&error)) return 1;
    engine.newProject();
    engine.setCompositionSize(QSize(100, 100));
    const int index = engine.addLayer("Lines");
    if (!engine.setLayerIsf(index, kLineGeneratorPath, &error)) {
        std::printf("FAIL line_ui: the Line generator does not load: %s\n", qPrintable(error));
        return 1;
    }
    const quint64 id = engine.layerId(index);
    int failures = 0;
    auto check = [&](bool ok, const char *reason) {
        if (!ok) {
            ++failures;
            std::printf("FAIL line_ui: %s\n", reason);
        }
    };
    auto near = [](QPointF a, QPointF b) { return std::abs(a.x() - b.x()) < 0.01 && std::abs(a.y() - b.y()) < 0.01; };

    QUndoStack undo;
    MappingView view(&engine);
    view.setUndoStack(&undo);
    view.setLayer(index);
    view.resize(600, 600);
    view.show();
    engine.renderFrame();
    app.processEvents();
    auto at = [](double x, double y) { return QPointF(24 + x * 552, 24 + y * 552); }; // the composition in the view
    auto send = [&](QEvent::Type type, QPointF p, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, p, view.mapToGlobal(p.toPoint()), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(&view, &event);
    };
    auto click = [&](QPointF p) {
        send(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    };
    auto doubleClick = [&](QPointF p) {
        click(p);
        send(QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    };
    auto key = [&](int k) {
        QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier);
        QApplication::sendEvent(&view, &press);
    };
    auto lines = [&] { return engine.layerLines(id); };
    int edited = 0;
    QObject::connect(&view, &MappingView::linesEdited, &view, [&] { ++edited; });

    // Drawn: three points, Enter
    view.startLineDrawing();
    check(view.drawingLine(), "drawing starts on a Line layer");
    click(at(0.2, 0.2));
    click(at(0.8, 0.2));
    click(at(0.8, 0.8));
    view.grabFramebuffer(); // painted while drawing
    key(Qt::Key_Return);
    check(!view.drawingLine() && lines().size() == 1 && lines()[0].points.size() == 3 && !lines()[0].closed,
          "Enter ends a line of three points");
    check(near(lines()[0].points[1], QPointF(0.8, 0.2)), "its points where they were clicked (the layer's frame)");
    check(edited == 1, "the view says a line was drawn");
    undo.undo();
    check(lines().empty(), "undo: no line");
    undo.redo();
    check(lines().size() == 1, "redo: the line again");

    // Backspace takes a point back, Esc cancels
    view.startLineDrawing();
    click(at(0.1, 0.9));
    click(at(0.3, 0.9));
    key(Qt::Key_Backspace);
    key(Qt::Key_Escape);
    check(!view.drawingLine() && lines().size() == 1, "Esc: nothing drawn");

    // Closed: its first point clicked again
    view.startLineDrawing();
    click(at(0.1, 0.5));
    click(at(0.3, 0.4));
    click(at(0.3, 0.6));
    click(at(0.1, 0.5));
    check(lines().size() == 2 && lines()[1].closed && lines()[1].points.size() == 3, "the first point again: closed");

    // A double-click ends it (its second press adds no point)
    view.startLineDrawing();
    click(at(0.5, 0.95));
    doubleClick(at(0.7, 0.95));
    check(lines().size() == 3 && lines()[2].points.size() == 2, "a double-click ends it");

    // A point dragged
    send(QEvent::MouseButtonPress, at(0.8, 0.2), Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, at(0.7, 0.3), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, at(0.7, 0.3), Qt::LeftButton, Qt::NoButton);
    check(near(lines()[0].points[1], QPointF(0.7, 0.3)) && near(lines()[0].points[0], QPointF(0.2, 0.2)), "a point dragged");
    undo.undo();
    check(near(lines()[0].points[1], QPointF(0.8, 0.2)), "undo: back");
    undo.redo();

    // A line dragged by a segment
    const QPointF before = lines()[2].points[0];
    send(QEvent::MouseButtonPress, at(0.6, 0.95), Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, at(0.6, 0.85), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, at(0.6, 0.85), Qt::LeftButton, Qt::NoButton);
    check(near(lines()[2].points[0], before - QPointF(0, 0.1)), "a line dragged whole by a segment");

    // A point added by double-clicking a segment
    send(QEvent::MouseButtonDblClick, at(0.45, 0.25), Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, at(0.45, 0.25), Qt::LeftButton, Qt::NoButton);
    check(lines()[0].points.size() == 4 && near(lines()[0].points[1], QPointF(0.45, 0.25)), "a point added on a segment");

    // Delete: the selected point, then a line
    click(at(0.45, 0.25));
    key(Qt::Key_Delete);
    check(lines()[0].points.size() == 3, "Delete removes the selected point");
    click(at(0.6, 0.85));
    key(Qt::Key_Delete);
    check(lines().size() == 2, "Delete removes the selected line");
    undo.undo();
    check(lines().size() == 3, "undo: the line back");

    // NYXMAP_SCREENSHOTS=<folder>: the view with its lines, as rendered and as edited
    if (const QByteArray dir = qgetenv("NYXMAP_SCREENSHOTS"); !dir.isEmpty()) {
        engine.setGeneratorSize(index, 100, 100);
        for (int i = 0; i < 5; ++i) engine.renderFrame();
        click(at(0.2, 0.2));
        view.grabFramebuffer().save(QString::fromLocal8Bit(dir) + "/line_view.png");
    }
    view.grabFramebuffer();
    engine.shutdown();
    std::printf("line_ui: %s (%d failure%s)\n", failures ? "FAIL" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
