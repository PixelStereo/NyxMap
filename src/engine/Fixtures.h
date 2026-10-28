#pragma once
// Pixel mapping: light fixtures placed on the composition, their pixels sampled at every frame and sent as DMX
// (Art-Net, sACN: Dmx.h). This file holds the model only — the types of fixture (their channels and their grid of
// pixels), the fixtures placed and patched, the DMX outputs — and the two pure steps between a picture and DMX:
// where each pixel is sampled, and the universes the sampled colours make. No OpenGL, no network.
//
// Coordinates: normalized composition, origin top left (as the mapping). Universes are the protocol's own numbers
// (Art-Net: the 15-bit port address, from 0; sACN: 1..63999). Addresses: 1..512.

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <array>
#include <map>
#include <optional>
#include <utility>
#include <vector>

// Cyan, Magenta, Yellow: subtractive colour mixing (1 - red, ...)
enum class ChannelRole { Dimmer, Red, Green, Blue, White, Amber, Uv, Fixed, Ignore, Cyan, Magenta, Yellow };
QString channelRoleName(ChannelRole r);
ChannelRole channelRoleFromName(const QString &s, bool *ok = nullptr);

struct FixtureChannel {
    ChannelRole role = ChannelRole::Ignore;
    bool fine = false; // 16 bits: two channels, coarse then fine
    quint8 value = 0;  // Fixed: the value sent (a fine Fixed channel sends 0 as its second byte)
    int width() const { return fine ? 2 : 1; }
    bool operator==(const FixtureChannel &o) const { return role == o.role && fine == o.fine && value == o.value; }
    QJsonValue toJson() const; // a role name alone when it is plain
    static FixtureChannel fromJson(const QJsonValue &v);
};

// One channel of a DMX map given channel by channel (FixtureType::dmxMap)
struct FixtureSlot {
    int offset = 0; // from the fixture's address (0: the address itself)
    FixtureChannel channel;
    int pixel = -1; // -1: the whole fixture
    bool operator==(const FixtureSlot &o) const { return offset == o.offset && channel == o.channel && pixel == o.pixel; }
};

// A kind of fixture: its grid of pixels and its channels — those of the whole fixture first (header), those of each
// pixel, then the last ones (footer). The pixels' channels follow one another in the order the grid gives.
// An imported fixture (GDTF: Gdtf.h) gives its DMX map channel by channel instead (dmxMap), and where each of its pixels
// is in the grid (cells).
//
// Values: a colour channel gets its pixel's colour (a channel of the whole fixture: the average of its pixels). A
// dimmer gets the fixture's intensity when its pixel (or the fixture) has colour channels, else the luminance.
struct FixtureType {
    enum class Start { TopLeft, TopRight, BottomLeft, BottomRight };
    QString id; // unique; the built-in ones are taken
    QString name;
    QString manufacturer;
    int cols = 1, rows = 1;
    bool byColumns = false; // pixels numbered column after column (else row after row)
    Start start = Start::TopLeft;
    bool snake = false; // every other row (or column) goes back
    std::vector<FixtureChannel> header, perPixel, footer;
    std::vector<FixtureSlot> dmxMap; // not empty: the DMX map as given (header, perPixel, footer and the order unused)
    std::vector<QPoint> cells;      // not empty: each pixel's cell in the grid (else the order above gives it)
    QJsonObject origin;             // where an imported type comes from (GDTF: file, fixture type id, mode)
    bool builtIn = false;

    int pixelCount() const { return cells.empty() ? cols * rows : int(cells.size()); }
    static int width(const std::vector<FixtureChannel> &chs);
    int channelCount() const;
    QPoint pixelCell(int k) const; // the pixel numbered k in DMX order: its column and row in the grid
    bool valid(QString *why = nullptr) const;
    QJsonObject toJson() const;
    static FixtureType fromJson(const QJsonObject &o);
};

// Dimmer, RGB, RGBW, Dimmer RGB: always there, not saved with the project
const std::vector<FixtureType> &builtInFixtureTypes();

struct Fixture {
    enum class White { Off, Min }; // RGBW: white left at 0, or the grey part of the colour (min of R, G, B) moved there
    quint64 id = 0;
    QString name;
    QString type; // FixtureType::id
    bool enabled = true;
    bool locked = false; // not moved, turned or resized in the views
    bool solo = false;   // soloed: the fixtures not soloed send black (their channels kept)
    QPointF center{0.5, 0.5};
    QSizeF size{0.1, 0.1};
    double rotation = 0; // degrees, clockwise on screen
    // Its own grid of pixels, for a type of one pixel (RGB, RGBW, ...): the type's channels repeated for each pixel
    // (its header and footer once), numbered in this order. Ignored for a type that has its own pixels
    int cols = 1, rows = 1;
    bool byColumns = false;
    FixtureType::Start start = FixtureType::Start::TopLeft;
    bool snake = false;
    quint64 output = 0;  // DmxOutput::id
    int universe = 0;    // an sACN output's fixtures: 1 at least (normalize)
    int address = 1;
    double intensity = 1; // 0..1, on every value sent (a header dimmer: its level)
    double gamma = 1;   // on the sampled colour (1: linear)
    White white = White::Min;
    // Its pixels along a path instead of in its rectangle (a LED strip): its points on the composition (normalized),
    // its pixels spread along it by length, in DMX order from its first point. A strip on a line of a Line generator
    // (lineLayer: that layer's id, lineIndex: which of its lines) follows it: the engine keeps its path where that
    // line is on the composition (and keeps its last place when the layer or the line goes)
    std::vector<QPointF> path;
    bool pathClosed = false;
    quint64 lineLayer = 0;
    int lineIndex = 0;
    bool onPath() const { return path.size() >= 2; }

    QJsonObject toJson() const;
    static Fixture fromJson(const QJsonObject &o);
};

struct DmxOutput {
    enum class Protocol { ArtNet, Sacn };
    quint64 id = 0;
    QString name;
    Protocol protocol = Protocol::ArtNet;
    bool enabled = true;
    // Unicast: to each of these IP addresses. Otherwise Art-Net broadcasts (on the chosen card's network), sACN sends
    // to each universe's multicast group
    bool unicast = false;
    QStringList destinations;
    QString interfaceAddress; // the local address sent from (its network card); empty: any
    int port = 0;        // 0: the protocol's (6454, 5568)
    double rate = 40;    // packets per second and universe
    int priority = 100;  // sACN: 0..200
    QString sourceName = QStringLiteral("NyxMap"); // sACN

    int effectivePort() const { return port > 0 ? port : protocol == Protocol::ArtNet ? 6454 : 5568; }
    bool operator==(const DmxOutput &o) const;
    bool operator!=(const DmxOutput &o) const { return !(*this == o); }
    QJsonObject toJson() const;
    static DmxOutput fromJson(const QJsonObject &o);
};

// The whole pixel mapping of a project
struct FixturePatch {
    std::vector<FixtureType> types; // the project's own (the built-in ones are not here)
    std::vector<Fixture> fixtures;
    std::vector<DmxOutput> outputs;

    const FixtureType *findType(const QString &id) const; // the project's, then the built-in ones
    // The type as the fixture uses it: with the fixture's own grid of pixels when its type has one pixel
    std::optional<FixtureType> resolvedType(const Fixture &f) const;
    const DmxOutput *findOutput(quint64 id) const;
    // Ids given where missing or taken, types colliding with a built-in one dropped, values clamped
    void normalize();
    QJsonObject toJson() const;
    static FixturePatch fromJson(const QJsonObject &o);
};

// A type a fixture can give a grid of pixels of its own (Fixture::cols, rows): one pixel, channels in grid form
bool takesFixtureGrid(const FixtureType &t);

// Where each pixel of the enabled fixtures (of a known type) is sampled, fixture after fixture, in DMX order.
// comp: the composition's size in pixels (the rotation turns in pixels, not in normalized units)
std::vector<QPointF> fixtureSamplePoints(const FixturePatch &p, QSize comp);
// The same for one fixture (whether enabled or not), and its corners (TL, TR, BR, BL)
std::vector<QPointF> fixturePixelPoints(const Fixture &f, const FixtureType &t, QSize comp);
std::array<QPointF, 4> fixtureCorners(const Fixture &f, QSize comp);

// The universes the colours sampled at those points (same order, 0..1) make, by output id and universe; a pixel's
// channels never straddle two universes: a pixel that would is moved to the start of the next one
using DmxKey = std::pair<quint64, int>; // output, universe
using DmxFrame = std::map<DmxKey, QByteArray>; // 512 bytes each
DmxFrame buildDmxFrame(const FixturePatch &p, const std::vector<std::array<float, 3>> &colors);
// What each pixel of the enabled fixtures gives out, from the same colours (0..1, by fixture id): its colour (a fixture
// without colour channels: the grey of its level), intensity and solo included — what a view shows
std::map<quint64, std::vector<std::array<float, 3>>> fixtureOutput(const FixturePatch &p,
                                                                   const std::vector<std::array<float, 3>> &colors);

// Where a fixture's channels land: one span per universe it uses. A type given channel by channel (dmxMap) stays in its
// universe: channels past 512 are not sent
struct DmxSpan {
    int universe = 0, first = 1, last = 1; // addresses, 1..512
};
std::vector<DmxSpan> fixtureFootprint(const FixtureType &t, int universe, int address);
