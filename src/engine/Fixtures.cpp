#include "Fixtures.h"

#include <QJsonArray>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace {

const char *const kRoleNames[] = {"dimmer", "red", "green", "blue", "white", "amber",
                                  "uv",     "fixed", "ignore", "cyan", "magenta", "yellow"};

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

// A fixture's channels where they land. A grid type in DMX order — the header, each pixel, the footer — each chunk
// placed where it starts (a chunk that would cross the end of a universe starts the next one); a type given channel
// by channel at its offsets, in its universe (channels past 512 dropped)
struct Placed {
    int universe, address;
    FixtureChannel channel;
    int pixel; // -1: the whole fixture
};

std::vector<Placed> place(const FixtureType &t, int universe, int address)
{
    std::vector<Placed> out;
    address = std::clamp(address, 1, 512);
    if (!t.dmxMap.empty()) {
        for (const FixtureSlot &s : t.dmxMap)
            if (address + s.offset + s.channel.width() - 1 <= 512)
                out.push_back({universe, address + s.offset, s.channel, s.pixel});
        return out;
    }
    int u = universe, a = address;
    auto chunk = [&](const std::vector<FixtureChannel> &chs, int pixel) {
        const int width = FixtureType::width(chs);
        if (width <= 0) return;
        if (a + width - 1 > 512) {
            ++u;
            a = 1;
        }
        for (const FixtureChannel &c : chs) {
            out.push_back({u, a, c, pixel});
            a += c.width();
        }
    };
    chunk(t.header, -1);
    for (int k = 0; k < t.pixelCount(); ++k) chunk(t.perPixel, k);
    chunk(t.footer, -1);
    return out;
}

bool isColor(ChannelRole r)
{
    switch (r) {
    case ChannelRole::Red: case ChannelRole::Green: case ChannelRole::Blue: case ChannelRole::White:
    case ChannelRole::Cyan: case ChannelRole::Magenta: case ChannelRole::Yellow: return true;
    default: return false;
    }
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

int FixtureType::channelCount() const
{
    if (dmxMap.empty()) return width(header) + pixelCount() * width(perPixel) + width(footer);
    int n = 0;
    for (const FixtureSlot &s : dmxMap) n = std::max(n, s.offset + s.channel.width());
    return n;
}

QPoint FixtureType::pixelCell(int k) const
{
    if (!cells.empty()) return k >= 0 && k < int(cells.size()) ? cells[size_t(k)] : QPoint();
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
    for (const FixtureSlot &s : dmxMap)
        if (s.offset < 0 || s.offset + s.channel.width() > 512 || s.pixel < -1 || s.pixel >= pixelCount())
            return fail(QStringLiteral("a channel out of the fixture"));
    for (const QPoint &c : cells)
        if (c.x() < 0 || c.y() < 0 || c.x() >= cols || c.y() >= rows) return fail(QStringLiteral("a pixel out of the grid"));
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
    if (dmxMap.empty()) o["perPixel"] = channelsToJson(perPixel);
    if (!footer.empty()) o["footer"] = channelsToJson(footer);
    if (!dmxMap.empty()) {
        QJsonArray a;
        for (const FixtureSlot &s : dmxMap) {
            QJsonObject j{{"offset", s.offset}, {"channel", s.channel.toJson()}};
            if (s.pixel >= 0) j["pixel"] = s.pixel;
            a.append(j);
        }
        o["slots"] = a;
    }
    if (!cells.empty()) {
        QJsonArray a;
        for (const QPoint &c : cells) a.append(QJsonArray{c.x(), c.y()});
        o["cells"] = a;
    }
    if (!origin.isEmpty()) o["origin"] = origin;
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
    for (const QJsonValue &v : o.value("slots").toArray()) {
        const QJsonObject j = v.toObject();
        t.dmxMap.push_back({j.value("offset").toInt(0), FixtureChannel::fromJson(j.value("channel")), j.value("pixel").toInt(-1)});
    }
    for (const QJsonValue &v : o.value("cells").toArray()) {
        const QJsonArray c = v.toArray();
        t.cells.emplace_back(c.at(0).toInt(0), c.at(1).toInt(0));
    }
    t.origin = o.value("origin").toObject();
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
    QJsonObject o{{"id", QString::number(id)}, {"name", name}, {"type", type}, {"enabled", enabled},
                  {"x", center.x()}, {"y", center.y()}, {"w", size.width()}, {"h", size.height()},
                  {"rotation", rotation}, {"output", QString::number(output)}, {"universe", universe},
                  {"address", address}, {"intensity", intensity}, {"gamma", gamma},
                  {"white", white == White::Off ? "off" : "min"}};
    if (locked) o["locked"] = true;
    if (solo) o["solo"] = true;
    if (cols != 1 || rows != 1 || byColumns || start != FixtureType::Start::TopLeft || snake)
        o["pixels"] = QJsonObject{{"cols", cols}, {"rows", rows}, {"order", byColumns ? "columns" : "rows"},
                                  {"start", startName(start)}, {"snake", snake}};
    return o;
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
    f.locked = o.value("locked").toBool(false);
    f.solo = o.value("solo").toBool(false);
    const QJsonObject px = o.value("pixels").toObject();
    f.cols = px.value("cols").toInt(1);
    f.rows = px.value("rows").toInt(1);
    f.byColumns = px.value("order").toString() == QLatin1String("columns");
    f.start = startFromName(px.value("start").toString());
    f.snake = px.value("snake").toBool(false);
    f.universe = o.value("universe").toInt(0);
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

bool takesFixtureGrid(const FixtureType &t) { return t.dmxMap.empty() && t.cells.empty() && t.pixelCount() == 1; }

std::optional<FixtureType> FixturePatch::resolvedType(const Fixture &f) const
{
    const FixtureType *t = findType(f.type);
    if (!t) return std::nullopt;
    FixtureType r = *t;
    if (takesFixtureGrid(r) && (f.cols > 1 || f.rows > 1)) {
        r.cols = f.cols;
        r.rows = f.rows;
        r.byColumns = f.byColumns;
        r.start = f.start;
        r.snake = f.snake;
    }
    return r;
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
        const DmxOutput *out = findOutput(f.output);
        f.universe = std::clamp(f.universe, out && out->protocol == DmxOutput::Protocol::Sacn ? 1 : 0, 63999);
        f.cols = std::clamp(f.cols, 1, 1024);
        f.rows = std::clamp(f.rows, 1, 1024);
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

namespace {
// A point of a fixture's frame (-0.5..0.5 across and down) on the composition, normalized: the rotation turns in
// composition pixels about the fixture's center (y down: a positive angle turns clockwise)
QPointF onComposition(const Fixture &f, QSize comp, double x, double y)
{
    const double cw = std::max(1, comp.width()), ch = std::max(1, comp.height());
    const double a = qDegreesToRadians(f.rotation), cs = std::cos(a), sn = std::sin(a);
    const double lx = x * f.size.width() * cw, ly = y * f.size.height() * ch;
    return {f.center.x() + (lx * cs - ly * sn) / cw, f.center.y() + (lx * sn + ly * cs) / ch};
}
} // namespace

std::vector<QPointF> fixturePixelPoints(const Fixture &f, const FixtureType &t, QSize comp)
{
    std::vector<QPointF> pts;
    for (int k = 0; k < t.pixelCount(); ++k) {
        const QPoint cell = t.pixelCell(k);
        pts.push_back(onComposition(f, comp, (cell.x() + 0.5) / t.cols - 0.5, (cell.y() + 0.5) / t.rows - 0.5));
    }
    return pts;
}

std::array<QPointF, 4> fixtureCorners(const Fixture &f, QSize comp)
{
    return {onComposition(f, comp, -0.5, -0.5), onComposition(f, comp, 0.5, -0.5), onComposition(f, comp, 0.5, 0.5),
            onComposition(f, comp, -0.5, 0.5)};
}

std::vector<QPointF> fixtureSamplePoints(const FixturePatch &p, QSize comp)
{
    std::vector<QPointF> pts;
    for (const Fixture &f : p.fixtures) {
        if (!active(p, f)) continue;
        const std::vector<QPointF> mine = fixturePixelPoints(f, *p.resolvedType(f), comp);
        pts.insert(pts.end(), mine.begin(), mine.end());
    }
    return pts;
}

std::vector<DmxSpan> fixtureFootprint(const FixtureType &t, int universe, int address)
{
    std::vector<DmxSpan> spans;
    std::vector<Placed> placed = place(t, universe, address);
    std::sort(placed.begin(), placed.end(), [](const Placed &a, const Placed &b) {
        return a.universe != b.universe ? a.universe < b.universe : a.address < b.address;
    });
    for (const Placed &p : placed) {
        const int last = p.address + p.channel.width() - 1;
        if (!spans.empty() && spans.back().universe == p.universe && spans.back().last + 1 >= p.address)
            spans.back().last = std::max(spans.back().last, last);
        else
            spans.push_back({p.universe, p.address, last});
    }
    return spans;
}

namespace {
// A fixture ready to send: its channels placed, the colours of its pixels (index pixels: the whole fixture, their
// average) with gamma and the white taken out, and what its pixels and the fixture carry
struct Prepared {
    struct Mix {
        float r = 0, g = 0, b = 0, w = 0, luma = 0;
    };
    FixtureType type;
    std::vector<Placed> placed;
    std::vector<Mix> mix;
    std::vector<char> hasColor;
    bool anyColor = false;
    float k = 1; // its intensity; 0 when another fixture is soloed
};

Prepared prepare(const Fixture &f, FixtureType t, const std::array<float, 3> *colors, bool soloed)
{
    Prepared p;
    const int pixels = t.pixelCount();
    p.placed = place(t, f.universe, f.address);
    p.type = std::move(t);
    p.hasColor.assign(size_t(pixels) + 1, 0);
    std::vector<char> hasWhite(size_t(pixels) + 1, 0);
    for (const Placed &c : p.placed) {
        const size_t g = c.pixel < 0 ? size_t(pixels) : size_t(c.pixel);
        p.hasColor[g] |= isColor(c.channel.role);
        hasWhite[g] |= c.channel.role == ChannelRole::White;
    }
    p.anyColor = std::find(p.hasColor.begin(), p.hasColor.end(), 1) != p.hasColor.end();
    p.mix.assign(size_t(pixels) + 1, {});
    Prepared::Mix &whole = p.mix[size_t(pixels)];
    for (int k = 0; k <= pixels; ++k) {
        Prepared::Mix &m = p.mix[size_t(k)];
        if (k < pixels) {
            const auto &c = colors[k];
            auto shape = [&](float v) { return std::pow(std::clamp(v, 0.0f, 1.0f), float(f.gamma)); };
            m.r = shape(c[0]);
            m.g = shape(c[1]);
            m.b = shape(c[2]);
            whole.r += m.r / pixels;
            whole.g += m.g / pixels;
            whole.b += m.b / pixels;
        }
        m.luma = 0.2126f * m.r + 0.7152f * m.g + 0.0722f * m.b;
        if (hasWhite[size_t(k)] && f.white == Fixture::White::Min) {
            m.w = std::min({m.r, m.g, m.b});
            m.r -= m.w;
            m.g -= m.w;
            m.b -= m.w;
        }
    }
    p.k = soloed ? float(f.intensity) : 0.0f;
    return p;
}

// Each enabled fixture of a known type with its colours: fn(fixture, prepared)
template <typename F>
void forEachPrepared(const FixturePatch &p, const std::vector<std::array<float, 3>> &colors, F fn)
{
    const bool anySolo = std::any_of(p.fixtures.begin(), p.fixtures.end(),
                                     [&](const Fixture &f) { return f.solo && active(p, f); });
    size_t next = 0; // the first colour of the current fixture
    for (const Fixture &f : p.fixtures) {
        if (!active(p, f)) continue;
        FixtureType t = *p.resolvedType(f);
        const size_t first = next;
        next += size_t(t.pixelCount());
        if (next > colors.size()) break; // sampled for another patch
        fn(f, prepare(f, std::move(t), colors.data() + first, !anySolo || f.solo));
    }
}
} // namespace

DmxFrame buildDmxFrame(const FixturePatch &p, const std::vector<std::array<float, 3>> &colors)
{
    DmxFrame frame;
    forEachPrepared(p, colors, [&](const Fixture &f, const Prepared &pr) {
        const DmxOutput *out = p.findOutput(f.output);
        if (!out) return;
        const int pixels = pr.type.pixelCount();
        const float k = pr.k;
        for (const Placed &c : pr.placed) {
            QByteArray &u = frame[{out->id, c.universe}];
            if (u.isEmpty()) u = QByteArray(512, '\0');
            const size_t g = c.pixel < 0 ? size_t(pixels) : size_t(c.pixel);
            const Prepared::Mix &m = pr.mix[g];
            const int a = c.address;
            float v = 0;
            switch (c.channel.role) {
            case ChannelRole::Dimmer: v = (c.pixel < 0 ? pr.anyColor : pr.hasColor[g]) ? k : m.luma * k; break;
            case ChannelRole::Red: v = m.r * k; break;
            case ChannelRole::Green: v = m.g * k; break;
            case ChannelRole::Blue: v = m.b * k; break;
            case ChannelRole::White: v = m.w * k; break;
            case ChannelRole::Cyan: v = 1.0f - m.r * k; break;
            case ChannelRole::Magenta: v = 1.0f - m.g * k; break;
            case ChannelRole::Yellow: v = 1.0f - m.b * k; break;
            default: break;
            }
            if (c.channel.role == ChannelRole::Fixed) {
                u[a - 1] = char(c.channel.value);
                if (c.channel.fine) u[a] = 0;
            } else if (c.channel.fine) {
                const int x = int(std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f));
                u[a - 1] = char(x >> 8);
                u[a] = char(x & 0xff);
            } else {
                u[a - 1] = char(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            }
        }
    });
    return frame;
}

std::map<quint64, std::vector<std::array<float, 3>>> fixtureOutput(const FixturePatch &p,
                                                                   const std::vector<std::array<float, 3>> &colors)
{
    std::map<quint64, std::vector<std::array<float, 3>>> out;
    forEachPrepared(p, colors, [&](const Fixture &f, const Prepared &pr) {
        std::vector<std::array<float, 3>> &px = out[f.id];
        const int pixels = pr.type.pixelCount();
        for (int k = 0; k < pixels; ++k) {
            const Prepared::Mix &m = pr.mix[size_t(k)];
            // A pixel without colour of its own shows the fixture's (its average), or the grey of its level
            const bool own = pr.hasColor[size_t(k)], whole = pr.hasColor[size_t(pixels)];
            const Prepared::Mix &c = own ? m : whole ? pr.mix[size_t(pixels)] : m;
            if (own || whole) px.push_back({(c.r + c.w) * pr.k, (c.g + c.w) * pr.k, (c.b + c.w) * pr.k});
            else px.push_back({m.luma * pr.k, m.luma * pr.k, m.luma * pr.k});
        }
    });
    return out;
}
