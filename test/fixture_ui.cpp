// Pixel mapping in the interface: fixtures added and edited in the patch panel, shown, picked, moved, turned, resized
// and deleted in the mapping view (undoable), and each preview's own switch. PINOL_SCREENSHOTS=<folder>: pictures of
// the view and the panel saved there.
#include "Commands.h"
#include "Engine.h"
#include "FixtureOverlay.h"
#include "FixturePanel.h"
#include "MappingView.h"
#include "PreviewWorkspace.h"

#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QElapsedTimer>
#include <QToolButton>
#include <QUndoStack>
#include <cmath>
#include <cstdio>

static void wait(int ms) // events handled meanwhile (the panel's refresh timer)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 10);
}

static int failures = 0;
static void check(bool ok, const char *why)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL fixture_ui: %s\n", why);
    }
}

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    app.setApplicationName("PinolFixtureUITest");
    Engine engine;
    QString error;
    if (!engine.initialize(&error)) return 1;
    engine.newProject();
    engine.setCompositionSize(QSize(100, 100));

    QUndoStack undo;
    FixturePanel panel(&engine, &undo);
    MappingView view(&engine);
    view.setUndoStack(&undo);
    QObject::connect(&panel, &FixturePanel::fixtureSelected, &view, &MappingView::setFixture);
    QObject::connect(&view, &MappingView::fixturePicked, &panel, &FixturePanel::selectFixture);
    view.resize(600, 600);
    view.show();
    panel.resize(900, 500);
    panel.show();
    engine.renderFrame();
    wait(100);

    // --- The panel: two fixtures at following addresses, an output made for the first
    auto *type = panel.findChild<QComboBox *>("fixturesNewType");
    auto *add = panel.findChild<QPushButton *>("fixturesAdd");
    check(type && add, "the panel's controls");
    if (!type || !add) return 1;
    type->setCurrentIndex(type->findData("rgb"));
    add->click();
    add->click();
    FixturePatch p = engine.fixturePatch();
    check(p.fixtures.size() == 2 && p.outputs.size() == 1, "two fixtures, one output made for them");
    if (p.fixtures.size() != 2) return 1;
    check(p.fixtures[0].address == 1 && p.fixtures[1].address == 4 && p.fixtures[1].output == p.outputs[0].id,
          "each new fixture at the next free address");
    undo.undo();
    check(engine.fixturePatch().fixtures.size() == 1, "adding a fixture is undone");
    undo.redo();
    cmd::editFixtures(nullptr, &engine, {}, [](FixturePatch &q) { q.outputs[0].enabled = false; }); // nothing on the network
    wait(100);
    auto *table = panel.findChild<QTableWidget *>("fixturesTable");
    check(table && table->rowCount() == 2, "the table lists the fixtures");
    if (table && table->rowCount() == 2) {
        table->item(0, 7)->setText("30"); // X, composition pixels
        check(std::abs(engine.fixturePatch().fixtures[0].center.x() - 0.3) < 1e-9, "a position typed in the table");
        table->item(1, 5)->setText("2"); // address 2: over the first fixture's channels
        wait(100);
        check(table->item(1, 5)->background().color().red() > 120, "an address conflict shows");
    }

    // --- The view: a fixture placed across the middle
    const quint64 id = engine.fixturePatch().fixtures[0].id;
    cmd::editFixtures(nullptr, &engine, {}, [&](FixturePatch &q) {
        q.fixtures[0].center = {0.5, 0.5};
        q.fixtures[0].size = {0.4, 0.1};
        q.fixtures[0].rotation = 0;
        q.fixtures[1].center = {0.5, 0.85};
        q.fixtures[1].size = {0.2, 0.2};
    });
    auto *show = view.findChild<QToolButton *>("mappingFixtures");
    check(show != nullptr, "the view's Fixtures switch");
    if (!show) return 1;
    show->setChecked(true);
    auto at = [](double x, double y) { return QPointF(24 + x * 552, 24 + y * 552); };
    auto toWidget = [&](QPointF n) { return at(n.x(), n.y()); };
    auto send = [&](QEvent::Type t, QPointF pos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QMouseEvent e(t, pos, view.mapToGlobal(pos.toPoint()), t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      buttons, mods);
        QApplication::sendEvent(&view, &e);
    };
    auto drag = [&](QPointF from, QPointF to, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, mods);
        send(QEvent::MouseMove, (from + to) / 2, Qt::LeftButton, mods);
        send(QEvent::MouseMove, to, Qt::LeftButton, mods);
        send(QEvent::MouseButtonRelease, to, Qt::NoButton, mods);
    };
    auto fixture = [&] { return engine.fixturePatch().fixtures[0]; };
    drag(at(0.5, 0.5), at(0.6, 0.55));
    check(view.fixture() == id && panel.selectedFixture() == id, "a click picks the fixture, in the panel too");
    check(std::abs(fixture().center.x() - 0.6) < 1e-6 && std::abs(fixture().center.y() - 0.55) < 1e-6, "dragged");
    undo.undo();
    check(std::abs(fixture().center.x() - 0.5) < 1e-6, "the drag is undone in one step");

    const QSize comp = engine.compositionSize();
    drag(fixtureview::rotateGrip(fixture(), comp, toWidget), at(0.9, 0.5), Qt::ShiftModifier);
    check(std::abs(fixture().rotation - 90) < 1e-6, "turned by its circle, by 15° steps with Shift");
    undo.undo();
    drag(fixtureview::resizeGrip(fixture(), comp, toWidget), at(0.8, 0.6));
    check(std::abs(fixture().size.width() - 0.6) < 1e-6 && std::abs(fixture().size.height() - 0.2) < 1e-6,
          "resized by its square, about its center");
    undo.undo();
    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
    QApplication::sendEvent(&view, &right);
    check(std::abs(fixture().center.x() - 0.6) < 1e-6, "Shift+Right moves it ten composition pixels");
    undo.undo();
    drag(at(0.05, 0.05), at(0.05, 0.05)); // nothing there
    check(view.fixture() == 0, "a click elsewhere drops the selection");

    if (const QByteArray dir = qgetenv("PINOL_SCREENSHOTS"); !dir.isEmpty()) {
        cmd::editFixtures(nullptr, &engine, {}, [&](FixturePatch &q) { // a bar of 8 x 2 pixels, turned
            FixtureType bar;
            bar.id = "bar8x2";
            bar.name = "Bar 8x2";
            bar.cols = 8;
            bar.rows = 2;
            bar.snake = true;
            bar.perPixel = {{ChannelRole::Red}, {ChannelRole::Green}, {ChannelRole::Blue}};
            q.types.push_back(bar);
            q.fixtures[1].type = "bar8x2";
            q.fixtures[1].address = 10;
            q.fixtures[1].size = {0.5, 0.1};
            q.fixtures[1].center = {0.45, 0.8};
            q.fixtures[1].rotation = -12;
        });
        view.setFixture(id);
        engine.renderFrame();
        wait(200);
        view.grabFramebuffer().save(QString::fromLocal8Bit(dir) + "/fixtures_view.png");
        panel.grab().save(QString::fromLocal8Bit(dir) + "/fixtures_panel.png");
    }
    view.setFixture(id);
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(&view, &del);
    check(engine.fixturePatch().fixtures.size() == 1 && view.fixture() == 0, "Delete removes the selected fixture");
    undo.undo();
    check(engine.fixturePatch().fixtures.size() == 2, "and comes back");
    show->setChecked(false);
    drag(at(0.5, 0.5), at(0.7, 0.5));
    check(std::abs(fixture().center.x() - 0.5) < 1e-6, "hidden fixtures are not picked");

    // --- Each preview has its own switch, for the composition and the viewports
    {
        auto *mapping = new MappingView(&engine); // the workspace takes it
        PreviewWorkspace previews(&engine, mapping);
        auto *second = previews.findChild<QToolButton *>("previewFixtures1");
        check(second != nullptr, "a preview's Fixtures switch");
    }

    engine.shutdown();
    std::printf("fixture_ui: %s (%d failure%s)\n", failures ? "FAIL" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
