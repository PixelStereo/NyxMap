// Pixel mapping in the interface: fixtures added and edited in the patch panel, shown, picked, moved, turned, resized
// and deleted in the mapping view (undoable), and each preview's own switch. NYXMAP_SCREENSHOTS=<folder>: pictures of
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
#include "ShowMenus.h"

#include <QApplication>
#include <QFile>
#include <QDir>
#include <QComboBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QTableWidget>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    app.setApplicationName("NyxMapFixtureUITest");
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
    check(table && table->rowCount() == 2 && table->columnCount() == 6, "the list: on, lock, solo, name, DMX, intensity");
    if (!table || table->rowCount() != 2) return 1;
    check(table->item(0, 4) && table->item(0, 4)->text() == "0 · 1-3", "the list shows the DMX address");

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
    check(table->item(0, 4)->text() == "0 · 1-12", "its address spans its pixels");
    emit panel.fixtureSelected(p.fixtures[1].id);
    inspector.setFixture(p.fixtures[1].id);
    inspector.findChild<QSpinBox *>("fixtureAddress")->setValue(4); // still free? no: the first now ends at 12
    wait(100);
    check(table->item(1, 4)->foreground().color() == QColor(255, 110, 95), "an address conflict shows");
    inspector.findChild<QSpinBox *>("fixtureAddress")->setValue(13);
    wait(100);
    check(table->item(1, 4)->foreground().color() != QColor(255, 110, 95), "and goes once moved");

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
    view.setShowFixtures(true);
    view.setMaskEditMode(false);
    view.raise(); view.activateWindow(); view.setFocus(); wait(100);
    view.zoomToFit();
    auto at = [&](double x, double y) { return view.compositionToWidget(QPointF(x, y)); };
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
    check(view.showFixtures(), "fixture interaction tool is active");
    const quint64 centerHit = fixtureview::hit(engine.fixturePatch(), engine.compositionSize(), toWidget, at(.5, .5));
    if (centerHit != id) {
        const QSize c = engine.compositionSize();
        const QPointF origin = at(0, 0), corner = at(1, 1);
        std::printf("fixture geometry: expected=%llu hit=%llu comp=%dx%d view=%dx%d origin=(%.3f,%.3f) corner=(%.3f,%.3f)\n",
                    static_cast<unsigned long long>(id), static_cast<unsigned long long>(centerHit),
                    c.width(), c.height(), view.width(), view.height(), origin.x(), origin.y(), corner.x(), corner.y());
        std::printf("fixture patch: %s\n", QJsonDocument(engine.fixturePatch().toJson()).toJson(QJsonDocument::Compact).constData());
        for (const auto &f : engine.fixturePatch().fixtures) {
            const auto corners = fixtureCorners(f, c);
            const auto polygon = fixtureview::outline(f, c, toWidget);
            for (int k = 0; k < 4; ++k)
                std::printf("fixture %llu corner %d: normalized=(%.9f,%.9f) widget=(%.9f,%.9f)\n",
                    (unsigned long long)f.id, k, corners[k].x(), corners[k].y(), polygon[k].x(), polygon[k].y());
        }
    }
    check(centerHit == id, "fixture hit geometry matches the view transform");
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

    if (const QByteArray dir = qgetenv("NYXMAP_SCREENSHOTS"); !dir.isEmpty()) {
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

    { // Fixture groups: their own values combine with their fixtures', a fixture put in one later obeys it, and it survives a save
        FixturePatch q = engine.fixturePatch();
        FixtureGroup g;
        g.id = 4242;
        g.name = "Stage left";
        g.enabled = false;
        g.intensity = .5;
        q.groups.push_back(g);
        Fixture f = q.fixtures.front();
        f.id = 9000;
        f.group = 4242;
        f.intensity = .8;
        f.solo = false;
        q.fixtures.push_back(f);
        const Fixture e = q.effectiveOf(f);
        check(!e.enabled, "a fixture of a disabled group does not send");
        check(std::abs(e.intensity - .4) < 1e-9, "its intensity times the group's");
        FixturePatch loaded = FixturePatch::fromJson(q.toJson());
        const FixtureGroup *back = loaded.findGroup(4242);
        check(back && back->name == "Stage left" && !back->enabled && std::abs(back->intensity - .5) < 1e-9, "a group keeps its name and values in the project");
        g.solo = true;
        q.groups.back() = g;
        check(q.effectiveOf(f).solo && !q.effectiveOf(q.fixtures.front()).solo, "a group's solo reaches its fixtures only");
    }
    engine.shutdown();
    // --- Fixed channels: one added to a fixture after its last, sent as set (not by the pixel mapping), a value of the
    // show (snapshots, presets, timelines, controls), kept in the project
    if (!engine.fixturePatch().fixtures.empty()) {
        const quint64 id = engine.fixturePatch().fixtures[0].id;
        engine.setParameter(0, QStringLiteral("fixture/%1/enable").arg(id), true);
        engine.setParameter(0, QStringLiteral("fixture/%1/solo").arg(id), false);
        inspector.setFixture(id);
        wait(50);
        const int before = engine.fixturePatch().resolvedType(engine.fixturePatch().fixtures[0])->channelCount();
        auto *add = inspector.findChild<QToolButton *>("fixtureAddChannel");
        check(add != nullptr, "the inspector adds a fixed channel");
        if (add) add->click();
        wait(50);
        FixturePatch fp = engine.fixturePatch();
        const std::vector<FixedChannel> fixed = fixedChannels(fp, *fp.findFixture(id));
        check(fixed.size() == 1 && fixed[0].channel == before + 1 && fixed[0].extra && fixed[0].value == 0,
              "the channel is added after the fixture's last, at 0");
        check(fp.resolvedType(*fp.findFixture(id))->channelCount() == before + 1, "the fixture takes one channel more");
        check(inspector.findChild<SliderField *>(QStringLiteral("fixtureChannel%1").arg(before + 1)) != nullptr,
              "its value is in the inspector");
        const QString key = QStringLiteral("ch%1").arg(before + 1);
        check(engine.setParameter(0, QStringLiteral("fixture/%1/%2").arg(id).arg(key), 200), "a value of the show: fixture/<id>/chN");
        fp = engine.fixturePatch();
        const Fixture &f = *fp.findFixture(id);
        FixturePatch alone = fp; // the other fixture starts on the channel added (a conflict): left out here
        alone.fixtures = {f};
        const DmxFrame frame = buildDmxFrame(alone, std::vector<std::array<float, 3>>(4096, {0.f, 0.f, 0.f}));
        const auto universe = frame.find({f.output, f.universe});
        check(universe != frame.end() && quint8(universe->second[f.address - 1 + before]) == 200, "sent as set, colours black");
        check(Fixture::fromJson(f.toJson()).channels == f.channels, "kept in the project");
        bool stored = false;
        for (const QJsonValue &v : engine.captureFixtures())
            if (v.toObject().value("id").toString().toULongLong() == id)
                stored = std::lround(v.toObject().value("params").toObject().value(key).toDouble()) == 200;
        check(stored, "a snapshot holds it");
        undo.undo(); // the channel added: taken back
        wait(50);
        fp = engine.fixturePatch();
        check(fp.resolvedType(*fp.findFixture(id))->channelCount() == before, "undone, the fixture has its channels again");

        // A channel of its type given a fixed value instead of the pixel mapping (its role in the editor), then back
        inspector.setFixture(id);
        wait(50);
        auto *role = inspector.findChild<QComboBox *>("channelRole1");
        check(role != nullptr, "each channel's role is in the inspector");
        if (role) {
            role->setCurrentIndex(role->findData(int(ChannelRole::Fixed)));
            wait(50);
            const std::map<int, FixtureChannel> own = engine.fixturePatch().findFixture(id)->channels;
            check(own.count(1) && own.at(1).role == ChannelRole::Fixed, "channel 1 sends a fixed value, not the pixel mapping");
            check(engine.setParameter(0, QStringLiteral("fixture/%1/ch1").arg(id), 77), "and is a value of the show");
            FixturePatch one = engine.fixturePatch();
            one.fixtures = {*one.findFixture(id)};
            const Fixture &g = one.fixtures[0];
            const DmxFrame sent = buildDmxFrame(one, std::vector<std::array<float, 3>>(4096, {1.f, 1.f, 1.f}));
            const auto u = sent.find({g.output, g.universe});
            check(u != sent.end() && quint8(u->second[g.address - 1]) == 77, "its value is sent, the colours white");
            undo.undo();
            wait(50);
            check(engine.fixturePatch().findFixture(id)->channels.empty(), "undone, it follows the pixel mapping again");
        }
    }
    { // A type's channels chosen before it is imported (or a fixture made): changed, one added
        const FixtureType t = *engine.fixturePatch().findType("rgb");
        std::vector<ChannelEditor::Row> rows = ChannelEditor::rowsOf(t);
        check(rows.size() == 3 && rows[0].now.role == ChannelRole::Red, "a type's channels as rows");
        if (rows.size() == 3) {
            rows[2].now = FixtureChannel{ChannelRole::Fixed, false, 9};
            rows.push_back({4, -1, FixtureChannel{ChannelRole::Fixed, false, 255}, std::nullopt});
            const FixtureType chosen = ChannelEditor::withRows(t, rows);
            check(chosen.channelCount() == 4 && chosen.dmxMap[2].channel.role == ChannelRole::Fixed &&
                      chosen.dmxMap[2].channel.value == 9 && chosen.dmxMap[3].channel.value == 255,
                  "the type takes the channels chosen");
            const std::map<int, FixtureChannel> own = ChannelEditor::ownChannels(rows);
            check(own.size() == 2 && own.count(3) && own.count(4), "a fixture keeps only what differs from its type");
        }
    }

    // --- Fixtures and sequences as owners of their own values: at the root of every menu, not the composition's
    {
        using O = showmenu::Owner;
        check(!O::composition().owns("fixture/5/intensity") && !O::composition().owns("sequence/3/go") &&
                  O::composition().owns("show/blackout"),
              "the composition does not hold the fixtures' or the sequences' values");
        check(O{O::Kind::Fixture, 5}.owns("fixture/5/ch4") && !O{O::Kind::Fixture, 5}.owns("fixture/55/ch4") &&
                  O{O::Kind::FixtureGroup, 5}.owns("fixture/g5/intensity") && O{O::Kind::Sequence, 3}.owns("sequence/3/next"),
              "a fixture, a group and a sequence each hold their own");
    }
    // --- A group's values are show values (an interaction, a timeline, a preset set them)
    if (!engine.fixturePatch().fixtures.empty()) {
        FixturePatch withGroup = engine.fixturePatch();
        FixtureGroup g;
        g.id = 900;
        g.name = "Front";
        withGroup.groups.push_back(g);
        withGroup.fixtures[0].group = 900;
        engine.setFixturePatch(withGroup);
        wait(200); // the list made again
        ParamInfo groupIntensity;
        check(engine.parameterInfo(0, "fixture/g900/intensity", &groupIntensity) && groupIntensity.label == "Fixtures › Front › Intensity",
              "a group's intensity: fixture/g<id>/intensity");
        check(engine.setParameter(0, "fixture/g900/solo", true), "a group's values are set as any value");
        const FixturePatch soloed = engine.fixturePatch();
        check(soloed.findGroup(900) && soloed.findGroup(900)->solo, "its solo set");
        // Its intensity in the list and the fixture's in the inspector: marked, with the right-click of any value
        bool groupWired = false;
        for (auto *slider : panel.findChildren<SliderField *>("fixtureIntensity"))
            groupWired |= slider->property("showValuePaths").toStringList() == QStringList{"fixture/g900/intensity"};
        check(groupWired, "the group's intensity in the list is a show value (right-click: interaction, timeline, preset)");
        const quint64 id = engine.fixturePatch().fixtures[0].id;
        inspector.setFixture(id);
        wait(50);
        auto *intensity = inspector.findChild<SliderField *>("fixtureInspectorIntensity");
        check(intensity && intensity->property("showValuePaths").toStringList() == QStringList{QStringLiteral("fixture/%1/intensity").arg(id)} &&
                  intensity->contextMenuPolicy() == Qt::CustomContextMenu,
              "the inspector's intensity too");
    }
    // --- Templates: a type made from nothing kept on this machine, offered again, a fixture of it copies it into the project
    {
        const QString file = QDir::temp().filePath(QStringLiteral("nyxmap-fixture-templates-%1.json").arg(QCoreApplication::applicationPid()));
        QFile::remove(file);
        qputenv("NYXMAP_FIXTURE_TEMPLATES", file.toLocal8Bit());
        FixtureType t;
        t.id = "custom-test";
        t.name = "Bar 4";
        t.cols = 4;
        t.header = {FixtureChannel{ChannelRole::Fixed, false, 255}};
        t.perPixel = {FixtureChannel{ChannelRole::Red, false, 0}, FixtureChannel{ChannelRole::Green, false, 0},
                      FixtureChannel{ChannelRole::Blue, false, 0}};
        check(saveFixtureTemplate(t), "a type made from nothing is kept as a template");
        const std::vector<FixtureType> kept = fixtureTemplates();
        check(kept.size() == 1 && kept[0].name == "Bar 4" && kept[0].channelCount() == 13 && kept[0].pixelCount() == 4,
              "offered again, its pixels and channels kept");
        const quint64 made = kept.empty() ? 0 : panel.addFixtureOfType(kept[0]);
        const FixturePatch withTemplate = engine.fixturePatch();
        check(made && withTemplate.findType("custom-test") && withTemplate.findFixture(made) &&
                  withTemplate.findFixture(made)->type == "custom-test",
              "a fixture of a template: its type copied into the project");
        check(!kept.empty() && templateOf(kept[0], "Copy").id != kept[0].id && templateOf(kept[0], "Copy").name == "Copy",
              "a fixture's type saved as a template gets an id of its own");
        check(removeFixtureTemplate("custom-test") && fixtureTemplates().empty(), "a template removed");
        QFile::remove(file);
        qunsetenv("NYXMAP_FIXTURE_TEMPLATES");
    }

    // --- A new project has a first sequence, and each sequence a Load as Next value; its controls are said to be gone
    int controlsSaid = 0;
    QObject::connect(&engine, &Engine::controlsChanged, &panel, [&controlsSaid] { ++controlsSaid; });
    engine.newProject();
    check(controlsSaid > 0, "a new project tells the Controls panel (its widgets go)");
    check(engine.sequencer().count() == 1 && engine.sequencer().sequence(0).name == "Sequence 1" && engine.sequencer().current() == 0,
          "a new project has Sequence 1, selected");
    {
        ParamInfo info;
        check(engine.parameterInfo(0, QStringLiteral("sequence/%1/next").arg(engine.sequencer().sequenceId(0)), &info) &&
                  info.label.endsWith("Load as Next"),
              "Load as Next is a value an interaction or a timeline drives");
    }

    std::printf("fixture_ui: %s (%d failure%s)\n", failures ? "FAIL" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
