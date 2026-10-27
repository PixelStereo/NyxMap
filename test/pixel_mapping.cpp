// Pixel mapping: the fixture model, the universes built from colours, the Art-Net and sACN packets, the composition
// sampled on the GPU, the packets received over the loopback, and the patch saved with the project.
#include "Engine.h"

#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <cmath>
#include <cstdio>

static int failures = 0;
static void check(bool ok, const char *why)
{
    if (!ok) {
        ++failures;
        std::printf("FAIL pixel_mapping: %s\n", why);
    }
}

static quint8 at(const QByteArray &u, int address) { return quint8(u.at(address - 1)); }

static FixturePatch onePatch(const QString &type, quint64 output = 1, int universe = 1, int address = 1)
{
    FixturePatch p;
    DmxOutput o;
    o.id = output;
    p.outputs.push_back(o);
    Fixture f;
    f.type = type;
    f.output = output;
    f.universe = universe;
    f.address = address;
    p.fixtures.push_back(f);
    p.normalize();
    return p;
}

static void modelTests()
{
    const auto &b = builtInFixtureTypes();
    check(b.size() == 4, "four built-in types");
    FixturePatch p;
    check(p.findType("dimmer") && p.findType("dimmer")->channelCount() == 1, "Dimmer: one channel");
    check(p.findType("rgb") && p.findType("rgb")->channelCount() == 3, "RGB: three channels");
    check(p.findType("rgbw") && p.findType("rgbw")->channelCount() == 4, "RGBW: four channels");
    check(p.findType("dimmer-rgb") && p.findType("dimmer-rgb")->channelCount() == 4, "Dimmer RGB: four channels");

    FixtureType bar;
    bar.id = "bar";
    bar.cols = 4;
    bar.rows = 2;
    bar.perPixel = {{ChannelRole::Red}, {ChannelRole::Green}, {ChannelRole::Blue}};
    check(bar.channelCount() == 24, "a 4x2 RGB grid has 24 channels");
    check(bar.pixelCell(5) == QPoint(1, 1), "rows: pixel 5 is column 1, row 1");
    bar.snake = true;
    check(bar.pixelCell(4) == QPoint(3, 1), "snake: the second row goes back");
    bar.start = FixtureType::Start::BottomRight;
    check(bar.pixelCell(0) == QPoint(3, 1), "starting bottom right");
    bar.snake = false;
    bar.start = FixtureType::Start::TopLeft;
    bar.byColumns = true;
    check(bar.pixelCell(3) == QPoint(1, 1), "columns: pixel 3 is column 1, row 1");

    // A pixel never straddles two universes
    FixtureType line;
    line.id = "line";
    line.cols = 200;
    line.perPixel = {{ChannelRole::Red}, {ChannelRole::Green}, {ChannelRole::Blue}};
    const auto spans = fixtureFootprint(line, 3, 1);
    check(spans.size() == 2 && spans[0].universe == 3 && spans[0].last == 510 && spans[1].universe == 4 &&
              spans[1].first == 1 && spans[1].last == 90,
          "200 RGB pixels: 170 in the first universe (510 channels), 30 in the next");

    // JSON
    FixturePatch q;
    FixtureType custom = bar;
    custom.header = {{ChannelRole::Dimmer, true, 0}};
    custom.footer = {{ChannelRole::Fixed, false, 42}};
    q.types.push_back(custom);
    FixtureType clash;
    clash.id = "rgb"; // a built-in id: dropped
    clash.perPixel = {{ChannelRole::Red}};
    q.types.push_back(clash);
    DmxOutput o;
    o.protocol = DmxOutput::Protocol::Sacn;
    o.destination = "10.0.0.5";
    o.priority = 150;
    q.outputs = {o, o};
    Fixture f;
    f.name = "Bar";
    f.type = "bar";
    f.rotation = 30;
    f.white = Fixture::White::Off;
    q.fixtures = {f, f};
    q.normalize();
    check(q.types.size() == 1, "a type taking a built-in id is dropped");
    check(q.outputs[0].id != q.outputs[1].id && q.fixtures[0].id != q.fixtures[1].id, "ids made unique");
    const QJsonObject json = q.toJson();
    const FixturePatch r = FixturePatch::fromJson(json);
    check(QJsonDocument(r.toJson()).toJson() == QJsonDocument(json).toJson(), "the patch survives JSON");
    check(r.types.size() == 1 && r.types[0].header.size() == 1 && r.types[0].header[0].fine &&
              r.types[0].footer[0].value == 42 && r.types[0].byColumns,
          "a custom type keeps its channels and its order");
    check(r.outputs[0].protocol == DmxOutput::Protocol::Sacn && r.outputs[0].priority == 150 &&
              r.outputs[0].effectivePort() == 5568,
          "an sACN output keeps its settings");
}

static void frameTests()
{
    using C = std::array<float, 3>;
    {
        const FixturePatch p = onePatch("rgb", 1, 2, 10);
        const DmxFrame f = buildDmxFrame(p, {C{1.0f, 0.5f, 0.0f}});
        const QByteArray u = f.count({1, 2}) ? f.at({1, 2}) : QByteArray();
        check(u.size() == 512 && at(u, 10) == 255 && at(u, 11) == 128 && at(u, 12) == 0 && at(u, 9) == 0,
              "RGB at address 10 of universe 2");
    }
    {
        const FixturePatch p = onePatch("rgbw");
        const QByteArray u = buildDmxFrame(p, {C{1.0f, 0.6f, 0.2f}}).at({1, 1});
        check(at(u, 1) == 204 && at(u, 2) == 102 && at(u, 3) == 0 && at(u, 4) == 51, "RGBW: the grey part goes to white");
        FixturePatch off = p;
        off.fixtures[0].white = Fixture::White::Off;
        const QByteArray v = buildDmxFrame(off, {C{1.0f, 0.6f, 0.2f}}).at({1, 1});
        check(at(v, 1) == 255 && at(v, 4) == 0, "RGBW with white off");
    }
    {
        const QByteArray u = buildDmxFrame(onePatch("dimmer"), {C{1.0f, 1.0f, 1.0f}}).at({1, 1});
        const QByteArray v = buildDmxFrame(onePatch("dimmer"), {C{0.0f, 1.0f, 0.0f}}).at({1, 1});
        check(at(u, 1) == 255 && at(v, 1) == 182, "Dimmer: the pixel's luminance");
    }
    {
        FixturePatch p = onePatch("dimmer-rgb");
        p.fixtures[0].intensity = 0.5;
        const QByteArray u = buildDmxFrame(p, {C{1.0f, 0.0f, 1.0f}}).at({1, 1});
        check(at(u, 1) == 128 && at(u, 2) == 128 && at(u, 3) == 0 && at(u, 4) == 128,
              "Dimmer RGB: the header dimmer is the intensity, on the colour too");
    }
    {
        FixturePatch p = onePatch("rgb");
        p.fixtures[0].gamma = 2.0;
        const QByteArray u = buildDmxFrame(p, {C{0.5f, 1.0f, 0.0f}}).at({1, 1});
        check(at(u, 1) == 64 && at(u, 2) == 255, "gamma on the sampled colour");
    }
    {
        FixturePatch p = onePatch("fine");
        FixtureType t;
        t.id = "fine";
        t.perPixel = {{ChannelRole::Red, true, 0}, {ChannelRole::Fixed, false, 7}};
        p.types.push_back(t);
        p.normalize();
        const QByteArray u = buildDmxFrame(p, {C{0.5f, 0.0f, 0.0f}}).at({1, 1});
        const int v = at(u, 1) << 8 | at(u, 2);
        check(v == 32768 && at(u, 3) == 7, "16-bit channel and a fixed value");
    }
    {
        FixturePatch p = onePatch("rgb", 1, 1, 511); // 511..513 does not fit: the next universe
        const DmxFrame f = buildDmxFrame(p, {C{1.0f, 1.0f, 1.0f}});
        check(f.count({1, 2}) && at(f.at({1, 2}), 1) == 255 && at(f.at({1, 2}), 3) == 255,
              "a pixel that would cross 512 starts the next universe");
        FixturePatch none = onePatch("rgb", 9);
        none.outputs[0].id = 3; // its output is gone
        check(buildDmxFrame(none, {C{1.0f, 1.0f, 1.0f}}).empty(), "a fixture without output sends nothing");
    }
    { // Points: a 2x1 fixture turned a quarter turn, in a 200x100 composition
        FixturePatch p = onePatch("pair");
        FixtureType t;
        t.id = "pair";
        t.cols = 2;
        t.perPixel = {{ChannelRole::Dimmer}};
        p.types.push_back(t);
        p.fixtures[0].center = {0.5, 0.5};
        p.fixtures[0].size = {0.5, 0.1}; // 100 px wide
        p.fixtures[0].rotation = 90;
        p.normalize();
        const auto pts = fixtureSamplePoints(p, {200, 100});
        check(pts.size() == 2 && std::abs(pts[0].x() - 0.5) < 1e-9 && std::abs(pts[0].y() - 0.25) < 1e-9 &&
                  std::abs(pts[1].y() - 0.75) < 1e-9,
              "a quarter turn lays the pixels from top to bottom, in pixels");
        p.fixtures[0].enabled = false;
        check(fixtureSamplePoints(p, {200, 100}).empty(), "a disabled fixture is not sampled");
    }
}

static void packetTests()
{
    QByteArray data(512, '\0');
    data[0] = char(11);
    data[511] = char(22);
    const QByteArray a = dmx::artDmxPacket(0x1234, 7, data);
    check(a.size() == 530 && a.startsWith(QByteArray("Art-Net\0", 8)), "ArtDmx: header and size");
    check(quint8(a[8]) == 0x00 && quint8(a[9]) == 0x50 && a[11] == 14 && a[12] == 7, "ArtDmx: OpDmx, version, sequence");
    check(quint8(a[14]) == 0x34 && quint8(a[15]) == 0x12 && quint8(a[16]) == 2 && quint8(a[17]) == 0,
          "ArtDmx: port address and length");
    check(a[18] == 11 && a[529] == 22, "ArtDmx: the slots");
    check(dmx::artDmxPacket(1, 1, QByteArray(3, '\1')).size() == 18 + 4, "ArtDmx: an odd length made even");

    const QUuid cid = QUuid::createUuid();
    const QByteArray s = dmx::sacnPacket(cid, "Pinol test", 120, 9, 300, data);
    auto u16 = [&](int i) { return quint8(s[i]) << 8 | quint8(s[i + 1]); };
    check(s.size() == 638, "E1.31: 638 bytes for 512 slots");
    check(u16(0) == 0x10 && s.mid(4, 12) == QByteArray("ASC-E1.17\0\0\0", 12), "E1.31: preamble and identifier");
    check(u16(16) == (0x7000 | 622) && u16(38) == (0x7000 | 600) && u16(115) == (0x7000 | 523), "E1.31: PDU lengths");
    check(quint8(s[21]) == 4 && quint8(s[43]) == 2 && quint8(s[117]) == 2 && quint8(s[118]) == 0xa1, "E1.31: vectors");
    check(s.mid(22, 16) == cid.toRfc4122() && s.mid(44, 10) == "Pinol test", "E1.31: CID and source name");
    check(quint8(s[108]) == 120 && quint8(s[111]) == 9 && s[112] == 0 && u16(113) == 300, "E1.31: priority, sequence, universe");
    check(u16(123) == 513 && s[125] == 0 && s[126] == 11 && s[637] == 22, "E1.31: start code and slots");
    check(quint8(dmx::sacnPacket(cid, "x", 100, 0, 1, data, true)[112]) == 0x40, "E1.31: stream terminated");
    check(dmx::sacnMulticastAddress(300) == "239.255.1.44", "E1.31: multicast group");
}

// The universe of output 1 once the GPU has sampled the composition (a few frames: read back without waiting)
static QByteArray sampled(Engine &e, quint64 output, int universe)
{
    QByteArray u;
    for (int i = 0; i < 30 && u.isEmpty(); ++i) {
        e.renderFrame();
        u = e.dmxUniverse(output, universe);
    }
    return u;
}

static void gpuTests(Engine &e, const QString &tmp)
{
    e.newProject();
    e.setCompositionSize({64, 32});
    QImage img(64, 32, QImage::Format_RGB32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) img.setPixel(x, y, x < 32 ? qRgb(255, 0, 0) : qRgb(0, 0, 255));
    const QString path = tmp + "/redblue.png";
    img.save(path);
    QString err;
    const int row = e.addLayer("Red Blue");
    check(e.setLayerImage(row, path, &err), "load the picture");

    FixturePatch p;
    FixtureType pair;
    pair.id = "pair";
    pair.cols = 2;
    pair.perPixel = {{ChannelRole::Red}, {ChannelRole::Green}, {ChannelRole::Blue}};
    p.types.push_back(pair);
    DmxOutput o;
    o.id = 1;
    o.enabled = false; // nothing on the network here
    p.outputs.push_back(o);
    Fixture f;
    f.type = "pair";
    f.output = 1;
    f.universe = 5;
    f.address = 100;
    f.size = {1.0, 0.5}; // the pixels at a quarter and three quarters of the width
    p.fixtures.push_back(f);
    e.setFixturePatch(p);
    QByteArray u = sampled(e, 1, 5);
    check(u.size() == 512, "the composition is sampled into the universe");
    if (u.size() == 512)
        check(at(u, 100) == 255 && at(u, 101) == 0 && at(u, 102) == 0 && at(u, 103) == 0 && at(u, 104) == 0 &&
                  at(u, 105) == 255,
              "left pixel red, right pixel blue");

    // Turned half a turn: the pixels swap
    p = e.fixturePatch();
    p.fixtures[0].rotation = 180;
    e.setFixturePatch(p);
    for (int i = 0; i < 6; ++i) e.renderFrame();
    u = e.dmxUniverse(1, 5);
    check(u.size() == 512 && at(u, 100) == 0 && at(u, 102) == 255 && at(u, 105) == 0, "half a turn swaps the pixels");

    // The composition's opacity reaches the lights
    e.fadeCompositionOpacity(0.0, 0.0);
    for (int i = 0; i < 6; ++i) e.renderFrame();
    u = e.dmxUniverse(1, 5);
    check(u.size() == 512 && at(u, 102) == 0, "composition opacity 0: the lights go out");
    e.fadeCompositionOpacity(1.0, 0.0);

    // Saved with the project, and back
    const QString file = tmp + "/mapping.pinol";
    check(e.saveProject(file, {}, &err), "save the project");
    const QJsonObject before = e.fixturePatch().toJson();
    e.newProject();
    check(e.fixturePatch().fixtures.empty() && e.fixturePatch().types.empty(), "a new project has no fixture");
    check(e.loadProject(file, nullptr, &err), "load the project");
    check(QJsonDocument(e.fixturePatch().toJson()).toJson() == QJsonDocument(before).toJson(),
          "the pixel mapping comes back with the project");
    e.newProject();
}

// Packets received over the loopback, from a real output
static void networkTests(Engine &e)
{
    for (const auto protocol : {DmxOutput::Protocol::ArtNet, DmxOutput::Protocol::Sacn}) {
        const bool art = protocol == DmxOutput::Protocol::ArtNet;
        e.newProject();
        e.setCompositionSize({16, 16});
        QUdpSocket rx;
        check(rx.bind(QHostAddress::LocalHost, 0), "a receiver on the loopback");
        FixturePatch p;
        DmxOutput o;
        o.id = 7;
        o.protocol = protocol;
        o.destination = "127.0.0.1";
        o.port = rx.localPort();
        o.rate = 50;
        p.outputs.push_back(o);
        Fixture f;
        f.type = "dimmer";
        f.output = 7;
        f.universe = 3;
        f.address = 1;
        p.fixtures.push_back(f);
        e.setFixturePatch(p);
        QByteArray packet;
        for (int i = 0; i < 100 && packet.isEmpty(); ++i) {
            e.renderFrame();
            if (rx.waitForReadyRead(20)) packet = rx.receiveDatagram().data();
        }
        check(!packet.isEmpty(), art ? "an Art-Net packet arrives" : "an sACN packet arrives");
        if (art) check(packet.size() == 530 && packet.startsWith("Art-Net") && quint8(packet[14]) == 3, "Art-Net: universe 3");
        else check(packet.size() == 638 && quint8(packet[113]) == 0 && quint8(packet[114]) == 3, "sACN: universe 3");
        check(e.dmxOutputState(7).level == DmxOutputState::Ok && e.dmxOutputState(7).packets > 0, "the output says it sends");
        if (!art) { // the stream is terminated when the output goes
            e.setFixturePatch(FixturePatch());
            bool terminated = false;
            for (int i = 0; i < 100 && !terminated; ++i) {
                e.renderFrame();
                while (rx.waitForReadyRead(10)) {
                    const QByteArray d = rx.receiveDatagram().data();
                    terminated = terminated || (d.size() > 112 && (quint8(d[112]) & 0x40));
                }
            }
            check(terminated, "sACN: the stream is terminated when the output is removed");
        }
    }
    e.newProject();
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QGuiApplication app(argc, argv);

    modelTests();
    frameTests();
    packetTests();

    Engine e;
    QString err;
    if (!e.initialize(&err)) {
        std::printf("FAIL OpenGL: %s\n", qPrintable(err));
        return 1;
    }
    QTemporaryDir tmp;
    gpuTests(e, tmp.path());
    networkTests(e);
    e.shutdown();
    std::printf("pixel_mapping: %s (%d failure%s)\n", failures ? "FAIL" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
