// Pixel mapping in the interface: fixtures added and edited in the patch panel, shown, picked, moved, turned, resized
// and deleted in the mapping view (undoable), and each preview's own switch. PINOL_SCREENSHOTS=<folder>: pictures of
// the view and the panel saved there.
#include "Commands.h"
#include "Engine.h"
#include "FixtureOverlay.h"
#include "DmxOutputsPanel.h"
#include "FixtureInspector.h"
#include "FixturePanel.h"
#include "Widgets.h"
#include "MappingView.h"
#include "PreviewWorkspace.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTableWidget>
#include <QElapsedTimer>
#include <QImage>
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
    FixtureInspector inspector(&engine, &undo);
    DmxOutputsPanel outputs(&engine, &undo);
    MappingView view(&engine);
    view.setUndoStack(&undo);
    QObject::connect(&panel, &FixturePanel::fixtureSelected, &view, &MappingView::setFixture);
    QObject::connect(&panel, &FixturePanel::fixtureSelected, &inspector, &FixtureInspector::setFixture);
    QObject::connect(&view, &MappingView::fixturePicked, &panel, &FixturePanel::selectFixture);
    view.resize(600, 600);
    view.show();
    panel.resize(520, 300);
    panel.show();
    inspector.show();
    outputs.show();
    engine.renderFrame();
    wait(100);

    // --- The list: two fixtures at following addresses of Art-Net's universe 0, an output made for the first
    check(panel.findChild<QToolButton *>("fixturesAdd") != nullptr, "the list's + button");
    const quint64 first = panel.addFixture("rgb");
    panel.addFixture("rgb");
    FixturePatch p = engine.fixturePatch();
    check(p.fixtures.size() == 2 && p.outputs.size() == 1, "two fixtures, one output made for them");
    if (p.fixtures.size() != 2) return 1;
    check(p.fixtures[0].universe == 0 && p.fixtures[0].address == 1 && p.fixtures[1].address == 4 &&
              p.fixtures[1].output == p.outputs[0].id,
          "each new fixture at the next free address, from Art-Net's universe 0");
    undo.undo();
    check(engine.fixturePatch().fixtures.size() == 1, "adding a fixture is undone");
    undo.redo();
    cmd::editFixtures(nullptr, &engine, {}, [](FixturePatch &q) { q.outputs[0].enabled = false; }); // nothing on the network
    wait(100);
    auto *outputTable = outputs.findChild<QTableWidget *>("dmxOutputsTable");
    check(outputTable && outputTable->rowCount() == 1, "the outputs, in their own panel");
    {
        // The selected output's settings: those of its protocol and mode only
        auto *mode = outputs.findChild<QComboBox *>("dmxOutputMode");
        auto *addresses = outputs.findChild<QLineEdit *>("dmxOutputAddresses");
        auto *priority = outputs.findChild<QSpinBox *>("dmxOutputPriority");
        auto *iface = outputs.findChild<QComboBox *>("dmxOutputInterface");
        check(mode && addresses && priority && iface, "the output's settings");
        if (mode && addresses && priority && iface) {
            check(iface->count() >= 1 && iface->itemData(0).toString().isEmpty(), "a menu of the network cards, Any first");
            check(mode->itemText(0) == "Broadcast" && !addresses->isVisibleTo(&outputs) && !priority->isVisibleTo(&outputs),
                  "Art-Net broadcast: no IP address, no priority");
            mode->setCurrentIndex(1);
            emit mode->activated(1);
            wait(100);
            check(engine.fixturePatch().outputs[0].unicast && addresses->isVisibleTo(&outputs), "unicast: the IP addresses");
            addresses->setText("2.0.0.10, 2.0.0.11");
            emit addresses->editingFinished();
            check(engine.fixturePatch().outputs[0].destinations == QStringList({"2.0.0.10", "2.0.0.11"}), "several IP addresses");
            cmd::editFixtures(&undo, &engine, {}, [](FixturePatch &q) { q.outputs[0].protocol = DmxOutput::Protocol::Sacn; });
            wait(100);
            check(mode->itemText(0) == "Multicast" && priority->isVisibleTo(&outputs), "sACN: multicast or unicast, a priority");
            priority->setValue(150);
            check(engine.fixturePatch().outputs[0].priority == 150, "the priority set");
            for (int k = 0; k < 4; ++k) undo.undo(); // back to Art-Net broadcast
            wait(100);
            check(!engine.fixturePatch().outputs[0].unicast &&
                      engine.fixturePatch().outputs[0].protocol == DmxOutput::Protocol::ArtNet,
                  "the output's edits are undone");
        }
    }
    auto *table = panel.findChild<QTableWidget *>("fixturesTable");
    check(table && table->rowCount() == 2 && table->columnCount() == 6, "the list: on, lock, solo, name, intensity, DMX");
    if (!table || table->rowCount() != 2) return 1;
    check(table->item(0, 5)->text() == "0 · 1-3", "the list shows the DMX address");

    // --- The inspector: the place, the grid of pixels, the address
    panel.selectFixture(first);
    emit panel.fixtureSelected(first);
    inspector.findChild<QDoubleSpinBox *>("fixtureX")->setValue(30);
    check(std::abs(engine.fixturePatch().fixtures[0].center.x() - 0.3) < 1e-9, "a position typed in the inspector");
    const double width = engine.fixturePatch().fixtures[0].size.width();
    inspector.findChild<QSpinBox *>("fixtureCols")->setValue(4);
    p = engine.fixturePatch();
    check(p.resolvedType(p.fixtures[0])->pixelCount() == 4 && std::abs(p.fixtures[0].size.width() - 4 * width) < 1e-9,
          "an RGB fixture given 4 pixels across, each keeping its size");
    wait(100);
    check(table->item(0, 5)->text() == "0 · 1-12", "its address spans its pixels");
    emit panel.fixtureSelected(p.fixtures[1].id);
    inspector.setFixture(p.fixtures[1].id);
    inspector.findChild<QSpinBox *>("fixtureAddress")->setValue(4); // still free? no: the first now ends at 12
    wait(100);
    check(table->item(1, 5)->foreground().color() == QColor(255, 110, 95), "an address conflict shows");
    inspector.findChild<QSpinBox *>("fixtureAddress")->setValue(13);
    wait(100);
    check(table->item(1, 5)->foreground().color() != QColor(255, 110, 95), "and goes once moved");

    // --- Lock and solo, from the list
    emit table->cellClicked(0, 1);
    check(engine.fixturePatch().fixtures[0].locked, "the padlock locks the fixture");
    emit table->cellClicked(1, 2);
    check(engine.fixturePatch().fixtures[1].solo, "S solos it");
    emit table->cellClicked(1, 2);
    check(!engine.fixturePatch().fixtures[1].solo, "and back");

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
    check(view.fixture() == id && std::abs(fixture().center.x() - 0.5) < 1e-6, "a locked fixture is picked, not moved");
    emit table->cellClicked(0, 1); // unlocked
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
            q.fixtures[1].address = 13;
            q.fixtures[1].size = {0.5, 0.1};
            q.fixtures[1].center = {0.45, 0.8};
            q.fixtures[1].rotation = -12;
        });
        // A rainbow under them: their pixels show what they send
        QImage rainbow(100, 100, QImage::Format_RGB32);
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 100; ++x) rainbow.setPixelColor(x, y, QColor::fromHsvF(x / 100.0, 1.0, 0.4 + 0.6 * y / 100.0));
        const QString picture = QString::fromLocal8Bit(dir) + "/rainbow.png";
        rainbow.save(picture);
        QString err;
        engine.setLayerImage(engine.addLayer("Rainbow"), picture, &err);
        for (int i = 0; i < 10; ++i) {
            engine.renderFrame();
            wait(10);
        }
        view.setFixture(id);
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
