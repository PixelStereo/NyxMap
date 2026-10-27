#include "Fixtures.h"

#include <QJsonArray>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace {

const char *const kRoleNames[] = {"dimmer", "red", "green", "blue", "white", "amber", "uv", "fixed", "ignore"};

const char *startName(FixtureType::Start s)
{
    switch (s) {
    case FixtureType::Start::TopRight: return "topRight";
    case FixtureType::Start::BottomLeft: return "bottomLeft";
    case FixtureType::Start::BottomRight: return "bottomRight";
    default: return "topLeft";
    }
}

FixtureType::Start startFromName(const QString &s)
{
    if (s == QLatin1String("topRight")) return FixtureType::Start::TopRight;
    if (s == QLatin1String("bottomLeft")) return FixtureType::Start::BottomLeft;
    if (s == QLatin1String("bottomRight")) return FixtureType::Start::BottomRight;
    return FixtureType::Start::TopLeft;
}

QJsonArray channelsToJson(const std::vector<FixtureChannel> &chs)
{
    QJsonArray a;
    for (const FixtureChannel &c : chs) a.append(c.toJson());
    return a;
}

std::vector<FixtureChannel> channelsFromJson(const QJsonValue &v)
{
    std::vector<FixtureChannel> chs;
    for (const QJsonValue &c : v.toArray()) chs.push_back(FixtureChannel::fromJson(c));
    return chs;
}

FixtureType builtIn(const char *id, const char *name, std::vector<ChannelRole> header, std::vector<ChannelRole> pixel)
{
    FixtureType t;
    t.id = QLatin1String(id);
    t.name = QLatin1String(name);
    t.manufacturer = QStringLiteral("Generic");
    t.builtIn = true;
    for (ChannelRole r : header) t.header.push_back({r, false, 0});
    for (ChannelRole r : pixel) t.perPixel.push_back({r, false, 0});
    return t;
}

// The chunks of a fixture's channels in DMX order — the header, each pixel, the footer — each placed where it starts
// (a chunk that would cross the end of a universe starts the next one). pixel: -1 header, pixelCount() footer
void placeChunks(const FixtureType &t, int universe, int address,
                 const std::function<void(int pixel, int universe, int address)> &fn)
{
    int u = universe, a = std::clamp(address, 1, 512);
    auto place = [&](int pixel, int width) {
        if (width <= 0) return;
        if (a + width - 1 > 512) {
            ++u;
            a = 1;
        }
        fn(pixel, u, a);
        a += width;
    };
    place(-1, FixtureType::width(t.header));
    const int w = FixtureType::width(t.perPixel);
    for (int k = 0; k < t.pixelCount(); ++k) place(k, w);
    place(t.pixelCount(), FixtureType::width(t.footer));
}

bool active(const FixturePatch &p, const Fixture &f) { return f.enabled && p.findType(f.type); }

} // namespace

QString channelRoleName(ChannelRole r) { return QLatin1String(kRoleNames[int(r)]); }

ChannelRole channelRoleFromName(const QString &s, bool *ok)
{
    for (int i = 0; i < int(std::size(kRoleNames)); ++i)
        if (s.compare(QLatin1String(kRoleNames[i]), Qt::CaseInsensitive) == 0) {
            if (ok) *ok = true;
            return ChannelRole(i);
        }
    if (ok) *ok = false;
    return ChannelRole::Ignore;
}

QJsonValue FixtureChannel::toJson() const
{
    if (!fine && role != ChannelRole::Fixed) return channelRoleName(role);
    QJsonObject o{{"role", channelRoleName(role)}};
    if (fine) o["fine"] = true;
    if (role == ChannelRole::Fixed) o["value"] = int(value);
    return o;
}

FixtureChannel FixtureChannel::fromJson(const QJsonValue &v)
{
    FixtureChannel c;
    if (v.isString()) {
        c.role = channelRoleFromName(v.toString());
        return c;
    }
    const QJsonObject o = v.toObject();
    c.role = channelRoleFromName(o.value("role").toString());
    c.fine = o.value("fine").toBool(false);
    c.value = quint8(std::clamp(o.value("value").toInt(0), 0, 255));
    return c;
}

int FixtureType::width(const std::vector<FixtureChannel> &chs)
{
    int n = 0;
    for (const FixtureChannel &c : chs) n += c.width();
    return n;
}

QPoint FixtureType::pixelCell(int k) const
{
    int c, r;
    if (!byColumns) {
        r = k / cols;
        c = k % cols;
        if (snake && (r & 1)) c = cols - 1 - c;
    } else {
        c = k / rows;
        r = k % rows;
        if (snake && (c & 1)) r = rows - 1 - r;
    }
    if (start == Start::TopRight || start == Start::BottomRight) c = cols - 1 - c;
    if (start == Start::BottomLeft || start == Start::BottomRight) r = rows - 1 - r;
    return {c, r};
}

bool FixtureType::valid(QString *why) const
{
    auto fail = [&](const QString &w) {
        if (why) *why = w;
        return false;
    };
    if (id.trimmed().isEmpty()) return fail(QStringLiteral("no id"));
    if (cols < 1 || rows < 1 || cols > 1024 || rows > 1024) return fail(QStringLiteral("grid out of 1..1024"));
    if (width(header) > 512 || width(perPixel) > 512 || width(footer) > 512)
        return fail(QStringLiteral("more than 512 channels in one part"));
    if (channelCount() == 0) return fail(QStringLiteral("no channel"));
    return true;
}

QJsonObject FixtureType::toJson() const
{
    QJsonObject o{{"id", id}, {"name", name}};
    if (!manufacturer.isEmpty()) o["manufacturer"] = manufacturer;
    o["pixels"] = QJsonObject{{"cols", cols}, {"rows", rows}, {"order", byColumns ? "columns" : "rows"},
                              {"start", startName(start)}, {"snake", snake}};
    if (!header.empty()) o["header"] = channelsToJson(header);
    o["perPixel"] = channelsToJson(perPixel);
    if (!footer.empty()) o["footer"] = channelsToJson(footer);
    return o;
}

FixtureType FixtureType::fromJson(const QJsonObject &o)
{
    FixtureType t;
    t.id = o.value("id").toString();
    t.name = o.value("name").toString(t.id);
    t.manufacturer = o.value("manufacturer").toString();
    const QJsonObject px = o.value("pixels").toObject();
    t.cols = px.value("cols").toInt(1);
    t.rows = px.value("rows").toInt(1);
    t.byColumns = px.value("order").toString() == QLatin1String("columns");
    t.start = startFromName(px.value("start").toString());
    t.snake = px.value("snake").toBool(false);
    t.header = channelsFromJson(o.value("header"));
    t.perPixel = channelsFromJson(o.value("perPixel"));
    t.footer = channelsFromJson(o.value("footer"));
    return t;
}

const std::vector<FixtureType> &builtInFixtureTypes()
{
    using R = ChannelRole;
    static const std::vector<FixtureType> types{
        builtIn("dimmer", "Dimmer", {}, {R::Dimmer}),
        builtIn("rgb", "RGB", {}, {R::Red, R::Green, R::Blue}),
        builtIn("rgbw", "RGBW", {}, {R::Red, R::Green, R::Blue, R::White}),
        builtIn("dimmer-rgb", "Dimmer RGB", {R::Dimmer}, {R::Red, R::Green, R::Blue}),
    };
    return types;
}

QJsonObject Fixture::toJson() const
{
    return QJsonObject{{"id", QString::number(id)}, {"name", name}, {"type", type}, {"enabled", enabled},
                       {"x", center.x()}, {"y", center.y()}, {"w", size.width()}, {"h", size.height()},
                       {"rotation", rotation}, {"output", QString::number(output)}, {"universe", universe},
                       {"address", address}, {"intensity", intensity}, {"gamma", gamma},
                       {"white", white == White::Off ? "off" : "min"}};
}

Fixture Fixture::fromJson(const QJsonObject &o)
{
    Fixture f;
    f.id = o.value("id").toString().toULongLong();
    f.name = o.value("name").toString();
    f.type = o.value("type").toString();
    f.enabled = o.value("enabled").toBool(true);
    f.center = {o.value("x").toDouble(0.5), o.value("y").toDouble(0.5)};
    f.size = {o.value("w").toDouble(0.1), o.value("h").toDouble(0.1)};
    f.rotation = o.value("rotation").toDouble(0);
    f.output = o.value("output").toString().toULongLong();
    f.universe = o.value("universe").toInt(1);
    f.address = o.value("address").toInt(1);
    f.intensity = o.value("intensity").toDouble(1);
    f.gamma = o.value("gamma").toDouble(1);
    f.white = o.value("white").toString() == QLatin1String("off") ? White::Off : White::Min;
    return f;
}

bool DmxOutput::operator==(const DmxOutput &o) const
{
    return id == o.id && name == o.name && protocol == o.protocol && enabled == o.enabled &&
           destination == o.destination && interfaceAddress == o.interfaceAddress && port == o.port &&
           rate == o.rate && priority == o.priority && sourceName == o.sourceName;
}

QJsonObject DmxOutput::toJson() const
{
    QJsonObject o{{"id", QString::number(id)}, {"name", name},
                  {"protocol", protocol == Protocol::Sacn ? "sacn" : "artnet"}, {"enabled", enabled},
                  {"destination", destination}, {"interface", interfaceAddress}, {"rate", rate}};
    if (port > 0) o["port"] = port;
    if (protocol == Protocol::Sacn) {
        o["priority"] = priority;
        o["source_name"] = sourceName;
    }
    return o;
}

DmxOutput DmxOutput::fromJson(const QJsonObject &o)
{
    DmxOutput d;
    d.id = o.value("id").toString().toULongLong();
    d.name = o.value("name").toString();
    d.protocol = o.value("protocol").toString() == QLatin1String("sacn") ? Protocol::Sacn : Protocol::ArtNet;
    d.enabled = o.value("enabled").toBool(true);
    d.destination = o.value("destination").toString().trimmed();
    d.interfaceAddress = o.value("interface").toString().trimmed();
    d.port = o.value("port").toInt(0);
    d.rate = o.value("rate").toDouble(40);
    d.priority = o.value("priority").toInt(100);
    d.sourceName = o.value("source_name").toString(QStringLiteral("Pinol"));
    return d;
}

const FixtureType *FixturePatch::findType(const QString &id) const
{
    for (const FixtureType &t : types)
        if (t.id == id) return &t;
    for (const FixtureType &t : builtInFixtureTypes())
        if (t.id == id) return &t;
    return nullptr;
}

const DmxOutput *FixturePatch::findOutput(quint64 id) const
{
    for (const DmxOutput &o : outputs)
        if (o.id == id) return &o;
    return nullptr;
}

void FixturePatch::normalize()
{
    std::set<QString> typeIds;
    for (const FixtureType &t : builtInFixtureTypes()) typeIds.insert(t.id);
    std::vector<FixtureType> kept;
    for (FixtureType t : types) {
        t.builtIn = false;
        t.cols = std::clamp(t.cols, 1, 1024);
        t.rows = std::clamp(t.rows, 1, 1024);
        if (!t.valid() || !typeIds.insert(t.id).second) continue;
        kept.push_back(std::move(t));
    }
    types = std::move(kept);

    auto giveIds = [](auto &items) {
        std::set<quint64> ids;
        quint64 next = 1;
        for (const auto &x : items) next = std::max(next, x.id + 1);
        for (auto &x : items)
            if (!x.id || !ids.insert(x.id).second) {
                x.id = next++;
                ids.insert(x.id);
            }
    };
    giveIds(outputs);
    for (DmxOutput &o : outputs) {
        o.rate = std::clamp(std::isfinite(o.rate) ? o.rate : 40.0, 1.0, 120.0);
        o.priority = std::clamp(o.priority, 0, 200);
        o.port = std::clamp(o.port, 0, 65535);
    }
    giveIds(fixtures);
    for (Fixture &f : fixtures) {
        f.universe = std::clamp(f.universe, 0, 63999);
        f.address = std::clamp(f.address, 1, 512);
        f.intensity = std::clamp(std::isfinite(f.intensity) ? f.intensity : 1.0, 0.0, 1.0);
        f.gamma = std::clamp(std::isfinite(f.gamma) ? f.gamma : 1.0, 0.1, 10.0);
        f.size = QSizeF(std::max(0.0, f.size.width()), std::max(0.0, f.size.height()));
    }
}

QJsonObject FixturePatch::toJson() const
{
    QJsonArray t, f, o;
    for (const FixtureType &x : types) t.append(x.toJson());
    for (const Fixture &x : fixtures) f.append(x.toJson());
    for (const DmxOutput &x : outputs) o.append(x.toJson());
    return QJsonObject{{"fixture_types", t}, {"fixtures", f}, {"dmx_outputs", o}};
}

FixturePatch FixturePatch::fromJson(const QJsonObject &o)
{
    FixturePatch p;
    for (const QJsonValue &v : o.value("fixture_types").toArray()) p.types.push_back(FixtureType::fromJson(v.toObject()));
    for (const QJsonValue &v : o.value("fixtures").toArray()) p.fixtures.push_back(Fixture::fromJson(v.toObject()));
    for (const QJsonValue &v : o.value("dmx_outputs").toArray()) p.outputs.push_back(DmxOutput::fromJson(v.toObject()));
    p.normalize();
    return p;
}

std::vector<QPointF> fixtureSamplePoints(const FixturePatch &p, QSize comp)
{
    std::vector<QPointF> pts;
    const double cw = std::max(1, comp.width()), ch = std::max(1, comp.height());
    for (const Fixture &f : p.fixtures) {
        if (!active(p, f)) continue;
        const FixtureType &t = *p.findType(f.type);
        const double a = qDegreesToRadians(f.rotation), cs = std::cos(a), sn = std::sin(a);
        for (int k = 0; k < t.pixelCount(); ++k) {
            const QPoint cell = t.pixelCell(k);
            // In composition pixels, about the fixture's center (y down: a positive angle turns clockwise)
            const double lx = ((cell.x() + 0.5) / t.cols - 0.5) * f.size.width() * cw;
            const double ly = ((cell.y() + 0.5) / t.rows - 0.5) * f.size.height() * ch;
            pts.emplace_back(f.center.x() + (lx * cs - ly * sn) / cw, f.center.y() + (lx * sn + ly * cs) / ch);
        }
    }
    return pts;
}

std::vector<DmxSpan> fixtureFootprint(const FixtureType &t, int universe, int address)
{
    std::vector<DmxSpan> spans;
    const int w = FixtureType::width(t.perPixel);
    placeChunks(t, universe, address, [&](int pixel, int u, int a) {
        const int width = pixel < 0 ? FixtureType::width(t.header) : pixel >= t.pixelCount() ? FixtureType::width(t.footer) : w;
        if (!spans.empty() && spans.back().universe == u && spans.back().last + 1 == a) spans.back().last = a + width - 1;
        else spans.push_back({u, a, a + width - 1});
    });
    return spans;
}

DmxFrame buildDmxFrame(const FixturePatch &p, const std::vector<std::array<float, 3>> &colors)
{
    DmxFrame frame;
    size_t next = 0; // the first colour of the current fixture
    for (const Fixture &f : p.fixtures) {
        if (!active(p, f)) continue;
        const FixtureType &t = *p.findType(f.type);
        const size_t first = next;
        next += size_t(t.pixelCount());
        if (next > colors.size()) break; // sampled for another patch
        const DmxOutput *out = p.findOutput(f.output);
        if (!out) continue;
        const bool hasWhite = std::any_of(t.perPixel.begin(), t.perPixel.end(),
                                          [](const FixtureChannel &c) { return c.role == ChannelRole::White; });
        const float k = float(f.intensity);
        auto write = [&](int universe, int address, const std::vector<FixtureChannel> &chs, int pixel) {
            QByteArray &u = frame[{out->id, universe}];
            if (u.isEmpty()) u = QByteArray(512, '\0');
            float r = 0, g = 0, b = 0, w = 0, luma = 0;
            if (pixel >= 0) {
                const auto &c = colors[first + size_t(pixel)];
                auto shape = [&](float v) { return std::pow(std::clamp(v, 0.0f, 1.0f), float(f.gamma)); };
                r = shape(c[0]);
                g = shape(c[1]);
                b = shape(c[2]);
                luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                if (hasWhite && f.white == Fixture::White::Min) {
                    w = std::min({r, g, b});
                    r -= w;
                    g -= w;
                    b -= w;
                }
            }
            int a = address;
            for (const FixtureChannel &ch : chs) {
                float v = 0;
                switch (ch.role) {
                case ChannelRole::Dimmer: v = (pixel >= 0 ? luma : 1.0f) * k; break;
                case ChannelRole::Red: v = r * k; break;
                case ChannelRole::Green: v = g * k; break;
                case ChannelRole::Blue: v = b * k; break;
                case ChannelRole::White: v = w * k; break;
                default: break;
                }
                if (ch.role == ChannelRole::Fixed) {
                    if (a <= 512) u[a - 1] = char(ch.value);
                    if (ch.fine && a + 1 <= 512) u[a] = 0;
                } else if (ch.fine) {
                    const int x = int(std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f));
                    if (a <= 512) u[a - 1] = char(x >> 8);
                    if (a + 1 <= 512) u[a] = char(x & 0xff);
                } else if (a <= 512) {
                    u[a - 1] = char(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
                }
                a += ch.width();
            }
        };
        placeChunks(t, f.universe, f.address, [&](int pixel, int u, int a) {
            if (pixel < 0) write(u, a, t.header, -1);
            else if (pixel >= t.pixelCount()) write(u, a, t.footer, -1);
            else write(u, a, t.perPixel, pixel);
        });
    }
    return frame;
}
