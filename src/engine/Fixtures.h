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
#include <QtGlobal>
#include <array>
#include <map>
#include <utility>
#include <vector>

enum class ChannelRole { Dimmer, Red, Green, Blue, White, Amber, Uv, Fixed, Ignore };
QString channelRoleName(ChannelRole r);
ChannelRole channelRoleFromName(const QString &s, bool *ok = nullptr);

struct FixtureChannel {
    ChannelRole role = ChannelRole::Ignore;
    bool fine = false; // 16 bits: two channels, coarse then fine
    quint8 value = 0;  // Fixed: the value sent
    int width() const { return fine ? 2 : 1; }
    bool operator==(const FixtureChannel &o) const { return role == o.role && fine == o.fine && value == o.value; }
    QJsonValue toJson() const; // a role name alone when it is plain
    static FixtureChannel fromJson(const QJsonValue &v);
};

// A kind of fixture: its grid of pixels and its channels — those of the whole fixture first (header), those of each
// pixel, then the last ones (footer). The pixels' channels follow one another in the order the grid gives.
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
    bool builtIn = false;

    int pixelCount() const { return cols * rows; }
    static int width(const std::vector<FixtureChannel> &chs);
    int channelCount() const { return width(header) + pixelCount() * width(perPixel) + width(footer); }
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
    QPointF center{0.5, 0.5};
    QSizeF size{0.1, 0.1};
    double rotation = 0; // degrees, clockwise on screen
    quint64 output = 0;  // DmxOutput::id
    int universe = 1;
    int address = 1;
    double intensity = 1; // 0..1, on every value (a header dimmer: its level)
    double gamma = 1;     // on the sampled colour (1: linear)
    White white = White::Min;

    QJsonObject toJson() const;
    static Fixture fromJson(const QJsonObject &o);
};

struct DmxOutput {
    enum class Protocol { ArtNet, Sacn };
    quint64 id = 0;
    QString name;
    Protocol protocol = Protocol::ArtNet;
    bool enabled = true;
    QString destination; // an IP address; empty: broadcast (Art-Net), multicast (sACN)
    QString interfaceAddress; // the local address sent from (its network card); empty: any
    int port = 0;        // 0: the protocol's (6454, 5568)
    double rate = 40;    // packets per second and universe
    int priority = 100;  // sACN: 0..200
    QString sourceName = QStringLiteral("Pinol"); // sACN

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
    const DmxOutput *findOutput(quint64 id) const;
    // Ids given where missing or taken, types colliding with a built-in one dropped, values clamped
    void normalize();
    QJsonObject toJson() const;
    static FixturePatch fromJson(const QJsonObject &o);
};

// Where each pixel of the enabled fixtures (of a known type) is sampled, fixture after fixture, in DMX order.
// comp: the composition's size in pixels (the rotation turns in pixels, not in normalized units)
std::vector<QPointF> fixtureSamplePoints(const FixturePatch &p, QSize comp);

// The universes the colours sampled at those points (same order, 0..1) make, by output id and universe; a pixel's
// channels never straddle two universes: a pixel that would is moved to the start of the next one
using DmxKey = std::pair<quint64, int>; // output, universe
using DmxFrame = std::map<DmxKey, QByteArray>; // 512 bytes each
DmxFrame buildDmxFrame(const FixturePatch &p, const std::vector<std::array<float, 3>> &colors);

// Where a fixture's channels land: one span per universe it uses
struct DmxSpan {
    int universe = 0, first = 1, last = 1; // addresses, 1..512
};
std::vector<DmxSpan> fixtureFootprint(const FixtureType &t, int universe, int address);
