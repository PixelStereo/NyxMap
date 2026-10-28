// Pixel mapping: the fixture model, the universes built from colours, the Art-Net and sACN packets, the composition
// sampled on the GPU, the packets received over the loopback, and the patch saved with the project.
#include "Engine.h"
#include "Gdtf.h"
#include "Zip.h"

#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QThread>
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
    o.unicast = true;
    o.destinations = {"10.0.0.5", "10.0.0.6"};
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
              r.outputs[0].effectivePort() == 5568 && r.outputs[0].unicast &&
              r.outputs[0].destinations == QStringList({"10.0.0.5", "10.0.0.6"}),
          "an sACN output keeps its settings, its unicast addresses too");
    const DmxOutput old = DmxOutput::fromJson(QJsonObject{{"protocol", "artnet"}, {"destination", "2.0.0.9"}});
    check(old.unicast && old.destinations == QStringList({"2.0.0.9"}), "a single destination before: unicast to it");
    check(!DmxOutput::fromJson(QJsonObject{{"protocol", "artnet"}, {"destination", ""}}).unicast, "none: broadcast");

    // The sender says what is wrong with an output's addresses
    DmxSender sender;
    DmxOutput bad;
    bad.id = 4;
    bad.unicast = true;
    bad.destinations = {"10.0.0.1", "not.an.ip"};
    DmxOutput empty;
    empty.id = 5;
    empty.unicast = true;
    sender.follow({bad, empty});
    for (int i = 0; i < 100 && sender.state(5).level == DmxOutputState::Off; ++i) QThread::msleep(10);
    check(sender.state(4).level == DmxOutputState::Error && sender.state(4).text.contains("not.an.ip"),
          "unicast to something that is not an IP address: an error");
    check(sender.state(5).level == DmxOutputState::Error, "unicast without any address: an error");
    sender.stop();
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

// A fixture's own grid of pixels, solo, what the pixels give out, the defaults
static void fixtureGridTests()
{
    using C = std::array<float, 3>;
    FixturePatch p = onePatch("rgb", 1, 0, 1);
    check(Fixture().universe == 0, "a new fixture starts at universe 0 (Art-Net's first)");
    p.fixtures[0].cols = 4;
    p.fixtures[0].rows = 2;
    p.fixtures[0].snake = true;
    p.fixtures[0].size = {0.4, 0.2};
    p.normalize();
    const auto t = p.resolvedType(p.fixtures[0]);
    check(t && t->pixelCount() == 8 && t->channelCount() == 24 && t->pixelCell(4) == QPoint(3, 1),
          "an RGB fixture of 4 x 2 pixels, snaking");
    std::vector<C> colors(8, C{0, 0, 0});
    colors[1] = {1, 0, 0};
    colors[7] = {0, 0, 1};
    const QByteArray u = buildDmxFrame(p, colors).at({1, 0});
    check(at(u, 4) == 255 && at(u, 24) == 255 && at(u, 1) == 0, "each of its pixels at its channels");
    check(fixtureSamplePoints(p, {100, 100}).size() == 8, "each of its pixels sampled");
    FixtureType bar;
    bar.id = "bar";
    bar.cols = 3;
    bar.perPixel = {{ChannelRole::Red}};
    p.types.push_back(bar);
    p.fixtures[0].type = "bar";
    p.normalize();
    check(p.resolvedType(p.fixtures[0])->pixelCount() == 3, "a type with its own pixels keeps them");

    // Solo: the others send black, their channels kept
    FixturePatch s = onePatch("dimmer");
    Fixture second = s.fixtures[0];
    second.id = 2;
    second.address = 2;
    second.solo = true;
    s.fixtures.push_back(second);
    s.normalize();
    QByteArray v = buildDmxFrame(s, {C{1, 1, 1}, C{1, 1, 1}}).at({1, 1});
    check(at(v, 1) == 0 && at(v, 2) == 255, "solo: the fixture not soloed sends black");
    s.fixtures[1].solo = false;
    v = buildDmxFrame(s, {C{1, 1, 1}, C{1, 1, 1}}).at({1, 1});
    check(at(v, 1) == 255 && at(v, 2) == 255, "no solo: both send");

    // What the pixels give out: the colour times the intensity; a dimmer, the grey of its level
    FixturePatch o = onePatch("rgbw");
    o.fixtures[0].intensity = 0.5;
    auto looks = fixtureOutput(o, {C{1.0f, 0.6f, 0.2f}});
    check(looks.count(o.fixtures[0].id) && std::abs(looks[o.fixtures[0].id][0][0] - 0.5f) < 1e-6 &&
              std::abs(looks[o.fixtures[0].id][0][1] - 0.3f) < 1e-6,
          "a pixel shows its colour (white given back) times the intensity");
    looks = fixtureOutput(onePatch("dimmer"), {C{0.0f, 1.0f, 0.0f}});
    check(!looks.empty() && std::abs(looks.begin()->second[0][0] - 0.7152f) < 1e-4 &&
              looks.begin()->second[0][0] == looks.begin()->second[0][2],
          "a dimmer shows the grey of its level");

    // JSON: the grid, lock, solo, the intensity; sACN universes from 1
    Fixture j;
    j.cols = 8;
    j.rows = 2;
    j.byColumns = true;
    j.start = FixtureType::Start::BottomRight;
    j.locked = true;
    j.solo = true;
    const Fixture back = Fixture::fromJson(j.toJson());
    check(back.cols == 8 && back.rows == 2 && back.byColumns && back.start == FixtureType::Start::BottomRight &&
              back.locked && back.solo,
          "a fixture's grid, lock and solo survive JSON");
    check(std::abs(Fixture::fromJson(Fixture{}.toJson()).intensity - 1.0) < 1e-9 &&
              Fixture{}.toJson().contains("intensity"), "the intensity saved as \"intensity\"");
    FixturePatch sacn = onePatch("rgb", 1, 0, 1);
    sacn.outputs[0].protocol = DmxOutput::Protocol::Sacn;
    sacn.normalize();
    check(sacn.fixtures[0].universe == 1, "an sACN output's fixtures start at universe 1");
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
    const QByteArray s = dmx::sacnPacket(cid, "NyxMap test", 120, 9, 300, data);
    auto u16 = [&](int i) { return quint8(s[i]) << 8 | quint8(s[i + 1]); };
    check(s.size() == 638, "E1.31: 638 bytes for 512 slots");
    check(u16(0) == 0x10 && s.mid(4, 12) == QByteArray("ASC-E1.17\0\0\0", 12), "E1.31: preamble and identifier");
    check(u16(16) == (0x7000 | 622) && u16(38) == (0x7000 | 600) && u16(115) == (0x7000 | 523), "E1.31: PDU lengths");
    check(quint8(s[21]) == 4 && quint8(s[43]) == 2 && quint8(s[117]) == 2 && quint8(s[118]) == 0xa1, "E1.31: vectors");
    check(s.mid(22, 16) == cid.toRfc4122() && s.mid(44, 11) == "NyxMap test", "E1.31: CID and source name");
    check(quint8(s[108]) == 120 && quint8(s[111]) == 9 && s[112] == 0 && u16(113) == 300, "E1.31: priority, sequence, universe");
    check(u16(123) == 513 && s[125] == 0 && s[126] == 11 && s[637] == 22, "E1.31: start code and slots");
    check(quint8(dmx::sacnPacket(cid, "x", 100, 0, 1, data, true)[112]) == 0x40, "E1.31: stream terminated");
    check(dmx::sacnMulticastAddress(300) == "239.255.1.44", "E1.31: multicast group");
}

// --- Zip and GDTF
static QByteArray deflateRaw(const QByteArray &data)
{
    const QByteArray z = qCompress(data, 9); // 4 bytes of size, a zlib stream: its 2-byte header, the data, 4 of Adler
    return z.mid(6, z.size() - 10);
}

static quint32 crcOf(const QByteArray &b)
{
    quint32 c = 0xffffffffu;
    for (const char ch : b) {
        c ^= quint8(ch);
        for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xffffffffu;
}

// A zip of these files, deflated (or stored)
static QByteArray makeZip(const std::vector<std::pair<QString, QByteArray>> &files, bool deflate)
{
    QByteArray out, dir;
    auto le16 = [](QByteArray &b, int v) { b.append(char(v & 0xff)).append(char((v >> 8) & 0xff)); };
    auto le32 = [&](QByteArray &b, quint32 v) { le16(b, int(v & 0xffff)); le16(b, int(v >> 16)); };
    for (const auto &[name, data] : files) {
        const QByteArray packed = deflate ? deflateRaw(data) : data;
        const QByteArray n = name.toUtf8();
        const quint32 local = quint32(out.size());
        le32(out, 0x04034b50); le16(out, 20); le16(out, 0); le16(out, deflate ? 8 : 0); le16(out, 0); le16(out, 0);
        le32(out, crcOf(data)); le32(out, quint32(packed.size())); le32(out, quint32(data.size()));
        le16(out, int(n.size())); le16(out, 0); out += n; out += packed;
        le32(dir, 0x02014b50); le16(dir, 20); le16(dir, 20); le16(dir, 0); le16(dir, deflate ? 8 : 0); le16(dir, 0); le16(dir, 0);
        le32(dir, crcOf(data)); le32(dir, quint32(packed.size())); le32(dir, quint32(data.size()));
        le16(dir, int(n.size())); le16(dir, 0); le16(dir, 0); le16(dir, 0); le16(dir, 0); le32(dir, 0); le32(dir, local);
        dir += n;
    }
    const quint32 at = quint32(out.size());
    out += dir;
    le32(out, 0x06054b50); le16(out, 0); le16(out, 0); le16(out, int(files.size())); le16(out, int(files.size()));
    le32(out, quint32(dir.size())); le32(out, at); le16(out, 0);
    return out;
}

static const char *kDescription = R"(<?xml version="1.0" encoding="UTF-8"?>
<GDTF DataVersion="1.2">
  <FixtureType Name="Bar4" LongName="Test Bar 4" Manufacturer="NyxMap" FixtureTypeID="0000-TEST">
    <Geometries>
      <Geometry Name="Base" Position="{1,0,0,0}{0,1,0,0}{0,0,1,0}{0,0,0,1}">
        <Axis Name="Head"><Beam Name="Beam"/></Axis>
        <GeometryReference Name="Pixel 1" Geometry="Pixel" Position="{1,0,0,0.3}{0,1,0,0}{0,0,1,0}{0,0,0,1}"><Break DMXBreak="1" DMXOffset="3"/></GeometryReference>
        <GeometryReference Name="Pixel 2" Geometry="Pixel" Position="{1,0,0,0.1}{0,1,0,0}{0,0,1,0}{0,0,0,1}"><Break DMXBreak="1" DMXOffset="6"/></GeometryReference>
        <GeometryReference Name="Pixel 3" Geometry="Pixel" Position="{1,0,0,-0.1}{0,1,0,0}{0,0,1,0}{0,0,0,1}"><Break DMXBreak="1" DMXOffset="9"/></GeometryReference>
        <GeometryReference Name="Pixel 4" Geometry="Pixel" Position="{1,0,0,-0.3}{0,1,0,0}{0,0,1,0}{0,0,0,1}"><Break DMXBreak="1" DMXOffset="12"/></GeometryReference>
      </Geometry>
      <Geometry Name="Panel">
        <GeometryReference Name="P1" Geometry="Pixel" Position="{1,0,0,-0.1}{0,1,0,0}{0,0,1,0.1}{0,0,0,1}"><Break DMXBreak="1" DMXOffset="1"/></GeometryReference>
        <GeometryReference Name="P2" Geometry="Pixel" Position="{1,0,0,0.1}{0,1,0,0}{0,0,1,0.1}{0,0,0,1}"><Break DMXBreak="1" DMXOffset="4"/></GeometryReference>
        <GeometryReference Name="P3" Geometry="Pixel" Position="{1,0,0,-0.1}{0,1,0,0}{0,0,1,-0.1}{0,0,0,1}"><Break DMXBreak="1" DMXOffset="7"/></GeometryReference>
        <GeometryReference Name="P4" Geometry="Pixel" Position="{1,0,0,0.1}{0,1,0,0}{0,0,1,-0.1}{0,0,0,1}"><Break DMXBreak="1" DMXOffset="10"/></GeometryReference>
      </Geometry>
      <Geometry Name="Pixel"><Beam Name="PixelBeam"/></Geometry>
    </Geometries>
    <DMXModes>
      <DMXMode Name="Pixel" Geometry="Base">
        <DMXChannels>
          <DMXChannel DMXBreak="1" Offset="1,2" Geometry="Beam" InitialFunction="Beam_Dimmer.Dimmer.Dimmer"><LogicalChannel Attribute="Dimmer"><ChannelFunction Name="Dimmer" Attribute="Dimmer" Default="0/2"/></LogicalChannel></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="1" Geometry="PixelBeam"><LogicalChannel Attribute="ColorAdd_R"><ChannelFunction Name="R" Default="0/1"/></LogicalChannel></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="2" Geometry="PixelBeam"><LogicalChannel Attribute="ColorAdd_G"><ChannelFunction Name="G" Default="0/1"/></LogicalChannel></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="3" Geometry="PixelBeam"><LogicalChannel Attribute="ColorAdd_B"><ChannelFunction Name="B" Default="0/1"/></LogicalChannel></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="15,16" Geometry="Head" InitialFunction="Head_Pan.Pan.Pan"><LogicalChannel Attribute="Pan"><ChannelFunction Name="Pan" Default="128/1"/></LogicalChannel></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="None" Geometry="Beam"><LogicalChannel Attribute="Shutter1"/></DMXChannel>
        </DMXChannels>
      </DMXMode>
      <DMXMode Name="Basic" Geometry="Base">
        <DMXChannels>
          <DMXChannel DMXBreak="1" Offset="1" Geometry="Beam"><LogicalChannel Attribute="Dimmer"><ChannelFunction Name="Dimmer" Default="0/1"/></LogicalChannel></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="2" Geometry="Beam"><LogicalChannel Attribute="ColorAdd_R"/></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="3" Geometry="Beam"><LogicalChannel Attribute="ColorAdd_G"/></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="4" Geometry="Beam"><LogicalChannel Attribute="ColorAdd_B"/></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="5" Geometry="Head" Default="64/1"><LogicalChannel Attribute="Tilt"/></DMXChannel>
          <DMXChannel DMXBreak="2" Offset="1" Geometry="Head"><LogicalChannel Attribute="Gobo1"/></DMXChannel>
        </DMXChannels>
      </DMXMode>
      <DMXMode Name="Matrix" Geometry="Panel">
        <DMXChannels>
          <DMXChannel DMXBreak="1" Offset="1" Geometry="PixelBeam"><LogicalChannel Attribute="ColorSub_C"/></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="2" Geometry="PixelBeam"><LogicalChannel Attribute="ColorSub_M"/></DMXChannel>
          <DMXChannel DMXBreak="1" Offset="3" Geometry="PixelBeam"><LogicalChannel Attribute="ColorSub_Y"/></DMXChannel>
        </DMXChannels>
      </DMXMode>
    </DMXModes>
  </FixtureType>
</GDTF>
)";

static void zipTests()
{
    QByteArray text;
    for (int i = 0; i < 2000; ++i) text += QByteArray::number(i * 7919 % 1000) + (i % 3 ? " pixel " : " mapping\n");
    bool ok = false;
    check(zip::inflate(deflateRaw(text), &ok) == text && ok, "inflate: dynamic Huffman blocks");
    check(zip::inflate(deflateRaw("ab"), &ok) == "ab" && ok, "inflate: a fixed Huffman block");
    check(zip::inflate(qCompress(text, 0).mid(6), &ok) == text && ok, "inflate: stored blocks");
    zip::inflate(QByteArray("\xff\xff\xff", 3), &ok);
    check(!ok, "inflate: malformed data refused");
    for (const bool deflate : {false, true}) {
        const QByteArray z = makeZip({{"models/a.3ds", QByteArray(100, 'x')}, {"description.xml", text}}, deflate);
        QString err;
        check(zip::names(z).size() == 2, "zip: the directory");
        check(zip::read(z, "Description.XML", &err) == text, deflate ? "zip: a deflated entry" : "zip: a stored entry");
        check(zip::read(z, "missing.xml", &err).isEmpty() && !err.isEmpty(), "zip: a missing entry");
        QByteArray damaged = z;
        damaged[43] = char(damaged[43] ^ 0x55); // in the first entry's data
        err.clear();
        check(zip::read(damaged, "models/a.3ds", &err).isEmpty() && !err.isEmpty(), "zip: a corrupt entry refused");
    }
    QString err;
    check(zip::read("not a zip", "x", &err).isEmpty() && !err.isEmpty(), "zip: not an archive");
}

static void gdtfTests()
{
    const QByteArray archive = makeZip({{"description.xml", kDescription}, {"thumbnail.png", QByteArray(64, '\0')}}, true);
    const gdtf::Import im = gdtf::readArchive(archive, "Test@Bar4.gdtf");
    check(im.error.isEmpty() && im.types.size() == 3, "GDTF: three modes, three types");
    if (im.types.size() != 3) return;
    const FixtureType &px = im.types[0], &basic = im.types[1], &matrix = im.types[2];
    check(px.id == "gdtf:0000-TEST:Pixel" && px.name == "Test Bar 4 (Pixel)" && px.manufacturer == "NyxMap" &&
              px.origin.value("mode").toString() == "Pixel",
          "GDTF: ids and names from the file and the mode");
    check(px.pixelCount() == 4 && px.cols == 4 && px.rows == 1 && px.channelCount() == 16,
          "GDTF: the geometry references are the pixels");
    check(px.pixelCell(0) == QPoint(3, 0) && px.pixelCell(3) == QPoint(0, 0), "GDTF: pixels laid out by position");
    check(!im.warnings.isEmpty(), "GDTF: a second DMX break is reported");

    FixturePatch p;
    p.types = im.types;
    DmxOutput o;
    o.id = 1;
    p.outputs.push_back(o);
    Fixture f;
    f.type = px.id;
    f.output = 1;
    f.universe = 1;
    f.address = 101;
    p.fixtures.push_back(f);
    p.normalize();
    check(p.types.size() == 3, "GDTF types are valid project types");
    using C = std::array<float, 3>;
    QByteArray u = buildDmxFrame(p, {C{1, 0, 0}, C{0, 1, 0}, C{0, 0, 1}, C{1, 1, 1}}).at({1, 1});
    check(at(u, 101) == 255 && at(u, 102) == 255, "GDTF: the master dimmer (16 bits) at full intensity");
    check(at(u, 103) == 255 && at(u, 104) == 0 && at(u, 107) == 255 && at(u, 111) == 255 && at(u, 112) == 255 &&
              at(u, 114) == 255,
          "GDTF: each pixel's RGB at its break offset");
    check(at(u, 115) == 128 && at(u, 116) == 0, "GDTF: another attribute (pan) sends its default");

    p.fixtures[0].type = basic.id;
    u = buildDmxFrame(p, {C{0.0f, 1.0f, 0.0f}}).at({1, 1});
    check(basic.pixelCount() == 1 && at(u, 101) == 255 && at(u, 103) == 255 && at(u, 105) == 64 && at(u, 106) == 0,
          "GDTF: a single cell: dimmer at intensity, colour, default of the others, break 2 left out");

    check(matrix.cols == 2 && matrix.rows == 2 && matrix.pixelCell(0) == QPoint(0, 0) && matrix.pixelCell(3) == QPoint(1, 1),
          "GDTF: a 2x2 panel, the top row first");
    p.fixtures[0].type = matrix.id;
    u = buildDmxFrame(p, {C{1, 0, 0}, C{0, 0, 0}, C{0, 0, 0}, C{0, 0, 0}}).at({1, 1});
    check(at(u, 101) == 0 && at(u, 102) == 255 && at(u, 103) == 255 && at(u, 104) == 255, "GDTF: CMY is 1 - RGB");

    const FixtureType back = FixtureType::fromJson(px.toJson());
    check(back.dmxMap == px.dmxMap && back.cells == px.cells && back.origin == px.origin, "an imported type survives JSON");
    check(!gdtf::readDescription("<GDTF><Nope/></GDTF>").error.isEmpty(), "GDTF: not a fixture type");
    check(!gdtf::readArchive("junk").error.isEmpty(), "GDTF: not an archive");
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

// Frames rendered until the universe says so (the GPU's readback trails by a frame or a few)
template <typename F> static QByteArray settle(Engine &e, quint64 output, int universe, F ok)
{
    QByteArray u;
    for (int i = 0; i < 60; ++i) {
        e.renderFrame();
        u = e.dmxUniverse(output, universe);
        if (u.size() == 512 && ok(u)) break;
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
    u = settle(e, 1, 5, [](const QByteArray &x) { return at(x, 102) == 255; });
    check(u.size() == 512 && at(u, 100) == 0 && at(u, 102) == 255 && at(u, 105) == 0, "half a turn swaps the pixels");

    // The composition's opacity reaches the lights
    e.fadeCompositionOpacity(0.0, 0.0);
    u = settle(e, 1, 5, [](const QByteArray &x) { return at(x, 102) == 0; });
    check(u.size() == 512 && at(u, 102) == 0, "composition opacity 0: the lights go out");
    e.fadeCompositionOpacity(1.0, 0.0);

    // Saved with the project, and back
    const QString file = tmp + "/mapping.nyxmap";
    check(e.saveProject(file, {}, &err), "save the project");
    const QJsonObject before = e.fixturePatch().toJson();
    e.newProject();
    check(e.fixturePatch().fixtures.empty() && e.fixturePatch().types.empty(), "a new project has no fixture");
    check(e.loadProject(file, nullptr, &err), "load the project");
    check(QJsonDocument(e.fixturePatch().toJson()).toJson() == QJsonDocument(before).toJson(),
          "the pixel mapping comes back with the project");
    e.newProject();
}

// The fixtures' values in the snapshots: stored, recalled (the intensity faded), left out, kept in a project
static void snapshotTests(Engine &e, const QString &tmp)
{
    e.newProject();
    FixturePatch p = onePatch("rgb");
    Fixture second = p.fixtures[0];
    second.id = 0;
    second.name = "Second";
    second.address = 10;
    p.fixtures.push_back(second);
    e.setFixturePatch(p);
    const quint64 a = e.fixturePatch().fixtures[0].id, b = e.fixturePatch().fixtures[1].id;
    const QString ia = QStringLiteral("fixture/%1/intensity").arg(a), ib = QStringLiteral("fixture/%1/intensity").arg(b);
    const QString ea = QStringLiteral("fixture/%1/enable").arg(a), sb = QStringLiteral("fixture/%1/solo").arg(b);
    check(e.parameterValue(0, ia).toDouble() == 1.0 && e.parameterValue(0, ea).toBool(), "a fixture's values are parameters");
    check(e.setParameter(0, ia, 0.25) && e.fixturePatch().fixtures[0].intensity == 0.25, "setting its intensity sets the fixture's");

    Engine::Snapshot m;
    m.name = "Look";
    m.fade = 0;
    m.fixtures = e.captureFixtures();
    check(m.fixtures.size() == 2 && m.fixtures[0].toObject().value("params").toObject().value("intensity").toDouble() == 0.25,
          "a snapshot holds each fixture's values");
    const int look = e.addSnapshot(m);

    e.setParameter(0, ia, 0.9);
    e.setParameter(0, ea, false);
    e.setParameter(0, sb, true);
    check(!e.fixturePatch().fixtures[0].enabled && e.fixturePatch().fixtures[1].solo, "on / off and solo set");
    e.recallSnapshot(look);
    e.renderFrame();
    FixturePatch now = e.fixturePatch();
    check(now.fixtures[0].intensity == 0.25 && now.fixtures[0].enabled && !now.fixtures[1].solo,
          "the recall puts the fixtures back as stored");

    // A fade: the intensity goes there over the snapshot's time, on / off at once
    Engine::Snapshot slow = e.snapshot(look);
    slow.fade = 0.5;
    e.setSnapshot(look, slow);
    e.setParameter(0, ia, 1.0);
    e.recallSnapshot(look);
    double mid = -1;
    for (int i = 0; i < 200; ++i) {
        e.renderFrame();
        QThread::msleep(5);
        const double v = e.fixturePatch().fixtures[0].intensity;
        if (v > 0.26 && v < 0.99) mid = v;
        if (v == 0.25) break;
    }
    check(mid > 0, "the intensity fades");
    check(e.fixturePatch().fixtures[0].intensity == 0.25, "and gets there");

    // Left out: left alone
    Engine::Snapshot out = e.snapshot(look);
    out.fade = 0;
    QJsonObject first = out.fixtures[0].toObject();
    first["included"] = false;
    out.fixtures[0] = first;
    e.setSnapshot(look, out);
    e.setParameter(0, ia, 0.7);
    e.recallSnapshot(look);
    e.renderFrame();
    check(e.fixturePatch().fixtures[0].intensity == 0.7, "a fixture left out of the snapshot is left alone");

    // With the project
    QString err;
    const QString file = tmp + "/snapshots.nyxmap";
    check(e.saveProject(file, {}, &err), "save the project with its snapshot");
    e.newProject();
    check(e.loadProject(file, nullptr, &err), "load it");
    check(e.snapshotCount() == 1 && e.snapshot(0).fixtures.size() == 2 &&
              !e.snapshot(0).fixtures[0].toObject().value("included").toBool(true),
          "the snapshot keeps its fixtures");
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
        o.unicast = true;
        o.destinations = {"127.0.0.1"};
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
    fixtureGridTests();
    zipTests();
    gdtfTests();

    Engine e;
    QString err;
    if (!e.initialize(&err)) {
        std::printf("FAIL OpenGL: %s\n", qPrintable(err));
        return 1;
    }
    QTemporaryDir tmp;
    gpuTests(e, tmp.path());
    snapshotTests(e, tmp.path());
    networkTests(e);
    e.shutdown();
    std::printf("pixel_mapping: %s (%d failure%s)\n", failures ? "FAIL" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
