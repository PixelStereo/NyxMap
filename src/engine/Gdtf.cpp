#include "Gdtf.h"
#include "Zip.h"

#include <QFile>
#include <QFileInfo>
#include <QVector3D>
#include <QXmlStreamReader>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace {

// --- description.xml as a tree
struct Node {
    QString tag;
    QXmlStreamAttributes attrs;
    std::vector<Node> kids;
    QString attr(const char *name) const { return attrs.value(QLatin1String(name)).toString(); }
    const Node *child(const char *t) const
    {
        for (const Node &k : kids)
            if (k.tag == QLatin1String(t)) return &k;
        return nullptr;
    }
};

bool parse(const QByteArray &xml, Node *root, QString *err)
{
    QXmlStreamReader r(xml);
    std::vector<Node *> stack{root};
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            Node *top = stack.back();
            top->kids.push_back({r.name().toString(), r.attributes(), {}});
            stack.push_back(&top->kids.back());
        } else if (r.isEndElement()) {
            stack.pop_back();
        }
    }
    if (r.hasError()) {
        if (err) *err = QStringLiteral("description.xml, line %1: %2").arg(r.lineNumber()).arg(r.errorString());
        return false;
    }
    return true;
}

// --- Geometry: names, positions (a GDTF matrix: "{a,b,c,d}{e,f,g,h}{i,j,k,l}{m,n,o,p}", rows, translation last)
using Mat = std::array<double, 16>;
constexpr Mat kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

Mat parseMatrix(const QString &s)
{
    QString t = s;
    t.remove(QLatin1Char('{'));
    t.replace(QLatin1Char('}'), QLatin1Char(','));
    const QStringList parts = t.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() < 16) return kIdentity;
    Mat m{};
    for (int i = 0; i < 16; ++i) m[size_t(i)] = parts[i].trimmed().toDouble();
    return m;
}

Mat multiply(const Mat &a, const Mat &b)
{
    Mat m{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) m[size_t(r * 4 + c)] += a[size_t(r * 4 + k)] * b[size_t(k * 4 + c)];
    return m;
}

QVector3D translation(const Mat &m) { return QVector3D(float(m[3]), float(m[7]), float(m[11])); }

struct Geometry {
    const Node *node = nullptr;
    QString top;  // the top-level geometry holding it
    Mat global{}; // in its top-level geometry's frame
};

struct Reference {
    QString name, target; // target: the top-level geometry it repeats
    std::map<int, int> breaks; // DMX break → offset (1-based)
    QVector3D position;
};

bool isGeometry(const Node &n) { return n.tag != QLatin1String("Break") && n.attrs.hasAttribute(QLatin1String("Name")); }

void indexGeometries(const Node &n, const QString &top, const Mat &parent, std::map<QString, Geometry> &out)
{
    const Mat m = multiply(parent, n.attrs.hasAttribute(QLatin1String("Position")) ? parseMatrix(n.attr("Position")) : kIdentity);
    out.emplace(n.attr("Name"), Geometry{&n, top, m});
    for (const Node &k : n.kids)
        if (isGeometry(k)) indexGeometries(k, top, m, out);
}

void collectReferences(const Node &n, const Mat &parent, std::vector<Reference> &out)
{
    const Mat m = multiply(parent, n.attrs.hasAttribute(QLatin1String("Position")) ? parseMatrix(n.attr("Position")) : kIdentity);
    if (n.tag == QLatin1String("GeometryReference")) {
        Reference r{n.attr("Name"), n.attr("Geometry"), {}, translation(m)};
        for (const Node &b : n.kids)
            if (b.tag == QLatin1String("Break")) {
                bool ok = false;
                const int brk = b.attr("DMXBreak").toInt(&ok);
                r.breaks[ok ? brk : 1] = b.attr("DMXOffset").toInt();
            }
        out.push_back(r);
    }
    for (const Node &k : n.kids)
        if (isGeometry(k)) collectReferences(k, m, out);
}

// --- Channels
ChannelRole roleOf(const QString &attribute)
{
    static const std::map<QString, ChannelRole> roles{
        {"Dimmer", ChannelRole::Dimmer},
        {"ColorAdd_R", ChannelRole::Red},       {"ColorRGB_Red", ChannelRole::Red},
        {"ColorAdd_G", ChannelRole::Green},     {"ColorRGB_Green", ChannelRole::Green},
        {"ColorAdd_B", ChannelRole::Blue},      {"ColorRGB_Blue", ChannelRole::Blue},
        {"ColorAdd_W", ChannelRole::White},     {"ColorAdd_WW", ChannelRole::White}, {"ColorAdd_CW", ChannelRole::White},
        {"ColorAdd_A", ChannelRole::Amber},     {"ColorAdd_UV", ChannelRole::Uv},
        {"ColorSub_C", ChannelRole::Cyan},      {"ColorSub_M", ChannelRole::Magenta}, {"ColorSub_Y", ChannelRole::Yellow},
    };
    const auto it = roles.find(attribute);
    return it != roles.end() ? it->second : ChannelRole::Fixed;
}

bool isColor(ChannelRole r)
{
    return r == ChannelRole::Red || r == ChannelRole::Green || r == ChannelRole::Blue || r == ChannelRole::White ||
           r == ChannelRole::Cyan || r == ChannelRole::Magenta || r == ChannelRole::Yellow;
}

// A DMX value as GDTF writes it ("32768/2", "128/1s", or a bare number) as `bytes` bytes, most significant first
std::vector<quint8> dmxBytes(const QString &value, int bytes)
{
    QString v = value.trimmed();
    int given = bytes;
    const int slash = v.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        QString n = v.mid(slash + 1);
        n.remove(QLatin1Char('s'));
        given = std::clamp(n.toInt(), 1, 8);
        v = v.left(slash);
    }
    quint64 x = v.toULongLong();
    if (given > bytes) x >>= 8 * (given - bytes);
    else if (given < bytes) x <<= 8 * (bytes - given);
    std::vector<quint8> out(static_cast<size_t>(bytes), 0);
    for (int i = bytes - 1; i >= 0; --i, x >>= 8) out[size_t(i)] = quint8(x & 0xff);
    return out;
}

struct Channel {
    QString geometry, attribute;
    std::vector<int> offsets; // 1-based, in its break
    QString brk;              // "1", "2", ... or "Overwrite"
    QString defaultValue;
    ChannelRole role = ChannelRole::Fixed;
};

Channel readChannel(const Node &c)
{
    Channel ch;
    ch.geometry = c.attr("Geometry");
    ch.brk = c.attrs.hasAttribute(QLatin1String("DMXBreak")) ? c.attr("DMXBreak") : QStringLiteral("1");
    for (const QString &o : c.attr("Offset").split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int v = o.trimmed().toInt(&ok);
        if (ok && v > 0) ch.offsets.push_back(v);
    }
    const Node *logical = c.child("LogicalChannel");
    if (logical) ch.attribute = logical->attr("Attribute");
    ch.role = roleOf(ch.attribute);
    // The default: the channel's own (GDTF 1.0), else its initial function's, else its first function's
    ch.defaultValue = c.attr("Default");
    if (ch.defaultValue.isEmpty() && logical) {
        const QString initial = c.attr("InitialFunction").section(QLatin1Char('.'), -1);
        const Node *first = nullptr;
        for (const Node &f : logical->kids) {
            if (f.tag != QLatin1String("ChannelFunction")) continue;
            if (!first) first = &f;
            if (!initial.isEmpty() && f.attr("Name") == initial) {
                first = &f;
                break;
            }
        }
        if (first) ch.defaultValue = first->attr("Default");
    }
    return ch;
}

// The pixels' cells from their positions: the two axes they spread most along (x across when it is one of them; the
// higher up, the higher the row), each distinct value a column or a row. Two pixels in one cell: one row, in order
std::vector<QPoint> layout(const std::vector<QVector3D> &pos, int *cols, int *rows)
{
    const int n = int(pos.size());
    auto fallback = [&] {
        *cols = n;
        *rows = 1;
        std::vector<QPoint> cells;
        for (int k = 0; k < n; ++k) cells.emplace_back(k, 0);
        return cells;
    };
    if (n <= 1) return fallback();
    std::array<float, 3> lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const QVector3D &p : pos)
        for (int a = 0; a < 3; ++a) {
            lo[size_t(a)] = std::min(lo[size_t(a)], p[a]);
            hi[size_t(a)] = std::max(hi[size_t(a)], p[a]);
        }
    std::array<int, 3> axes{0, 1, 2};
    std::sort(axes.begin(), axes.end(), [&](int a, int b) { return hi[size_t(a)] - lo[size_t(a)] > hi[size_t(b)] - lo[size_t(b)]; });
    const float spread = hi[size_t(axes[0])] - lo[size_t(axes[0])];
    if (spread <= 1e-9f) return fallback();
    const float eps = spread * 1e-3f;
    int across = axes[0], down = hi[size_t(axes[1])] - lo[size_t(axes[1])] > eps ? axes[1] : -1;
    if (down == 0) std::swap(across, down);
    auto ranks = [&](int axis, bool descending) {
        std::vector<float> values;
        for (const QVector3D &p : pos) values.push_back(p[axis]);
        std::sort(values.begin(), values.end());
        std::vector<float> distinct;
        for (float v : values)
            if (distinct.empty() || v - distinct.back() > eps) distinct.push_back(v);
        std::vector<int> rank;
        for (const QVector3D &p : pos) {
            int i = 0;
            while (i + 1 < int(distinct.size()) && p[axis] - distinct[size_t(i)] > eps) ++i;
            rank.push_back(descending ? int(distinct.size()) - 1 - i : i);
        }
        return std::make_pair(rank, int(distinct.size()));
    };
    const auto [cx, nc] = ranks(across, false);
    std::vector<int> ry(size_t(n), 0);
    int nr = 1;
    if (down >= 0) std::tie(ry, nr) = ranks(down, true);
    std::set<std::pair<int, int>> taken;
    std::vector<QPoint> cells;
    for (int k = 0; k < n; ++k) {
        if (!taken.insert({cx[size_t(k)], ry[size_t(k)]}).second) return fallback();
        cells.emplace_back(cx[size_t(k)], ry[size_t(k)]);
    }
    *cols = nc;
    *rows = nr;
    return cells;
}

void addChannel(FixtureType &t, const Channel &c, int base, int pixel)
{
    const int n = int(c.offsets.size());
    if (c.role == ChannelRole::Fixed) {
        const std::vector<quint8> bytes = dmxBytes(c.defaultValue, n);
        for (int i = 0; i < n; ++i)
            t.dmxMap.push_back({base + c.offsets[size_t(i)] - 1, {ChannelRole::Fixed, false, bytes[size_t(i)]}, pixel});
        return;
    }
    t.dmxMap.push_back({base + c.offsets[0] - 1, {c.role, n >= 2, 0}, pixel});
    if (n >= 2 && c.offsets[1] != c.offsets[0] + 1) { // fine not right after coarse: placed apart
        t.dmxMap.back().channel.fine = false;
        t.dmxMap.push_back({base + c.offsets[1] - 1, {ChannelRole::Fixed, false, 0}, pixel}); // (its fine part: 0)
    }
    for (int i = 2; i < n; ++i) t.dmxMap.push_back({base + c.offsets[size_t(i)] - 1, {ChannelRole::Fixed, false, 0}, pixel});
}

} // namespace

namespace gdtf {

Import readDescription(const QByteArray &xml, const QString &fileName)
{
    Import result;
    Node root;
    if (!parse(xml, &root, &result.error)) return result;
    const Node *gdtfNode = root.child("GDTF");
    const Node *ft = gdtfNode ? gdtfNode->child("FixtureType") : nullptr;
    if (!ft) {
        result.error = QStringLiteral("Not a GDTF description (no GDTF/FixtureType)");
        return result;
    }
    const QString name = ft->attr("LongName").isEmpty() ? ft->attr("Name") : ft->attr("LongName");
    const QString manufacturer = ft->attr("Manufacturer");
    const QString typeId = ft->attr("FixtureTypeID").isEmpty() ? manufacturer + QLatin1Char('.') + ft->attr("Name")
                                                              : ft->attr("FixtureTypeID");
    std::map<QString, Geometry> geometries;
    std::map<QString, const Node *> tops;
    if (const Node *gs = ft->child("Geometries"))
        for (const Node &g : gs->kids)
            if (isGeometry(g)) {
                tops[g.attr("Name")] = &g;
                indexGeometries(g, g.attr("Name"), kIdentity, geometries);
            }
    const Node *modes = ft->child("DMXModes");
    if (!modes) {
        result.error = QStringLiteral("No DMX mode in %1").arg(name);
        return result;
    }

    for (const Node &mode : modes->kids) {
        if (mode.tag != QLatin1String("DMXMode")) continue;
        const QString modeName = mode.attr("Name");
        auto warn = [&](const QString &w) { result.warnings << QStringLiteral("%1 (%2): %3").arg(name, modeName, w); };
        std::vector<Reference> refs;
        if (const auto top = tops.find(mode.attr("Geometry")); top != tops.end())
            collectReferences(*top->second, kIdentity, refs);
        std::vector<Channel> channels;
        if (const Node *list = mode.child("DMXChannels"))
            for (const Node &c : list->kids)
                if (c.tag == QLatin1String("DMXChannel")) {
                    Channel ch = readChannel(c);
                    if (!ch.offsets.empty()) channels.push_back(ch); // no offset: a virtual channel
                }
        auto topOf = [&](const QString &g) {
            const auto it = geometries.find(g);
            return it != geometries.end() ? it->second.top : QString();
        };
        // Template channels: on a geometry repeated by references of the mode's geometry
        std::set<QString> templated;
        for (const Channel &c : channels) {
            const QString top = topOf(c.geometry);
            if (top != mode.attr("Geometry") &&
                std::any_of(refs.begin(), refs.end(), [&](const Reference &r) { return r.target == top; }))
                templated.insert(top);
        }

        FixtureType t;
        t.id = QStringLiteral("gdtf:%1:%2").arg(typeId, modeName);
        t.name = QStringLiteral("%1 (%2)").arg(name, modeName);
        t.manufacturer = manufacturer;
        t.origin = QJsonObject{{"format", "gdtf"}, {"file", fileName}, {"fixture_type_id", typeId}, {"mode", modeName}};
        std::vector<QVector3D> positions;
        bool skippedBreak = false;
        auto breakOne = [&](const Channel &c) {
            const bool one = c.brk == QLatin1String("1") || c.brk == QLatin1String("Overwrite");
            skippedBreak |= !one;
            return one;
        };
        if (!templated.empty()) {
            std::map<int, int> pixelOf; // reference index → pixel
            for (int i = 0; i < int(refs.size()); ++i)
                if (templated.count(refs[size_t(i)].target)) {
                    pixelOf[i] = int(positions.size());
                    positions.push_back(refs[size_t(i)].position);
                }
            for (const Channel &c : channels) {
                const QString top = topOf(c.geometry);
                if (!templated.count(top)) {
                    if (breakOne(c)) addChannel(t, c, 0, -1);
                    continue;
                }
                for (const auto &[i, pixel] : pixelOf) {
                    const Reference &r = refs[size_t(i)];
                    if (r.target != top || r.breaks.empty()) continue;
                    // Overwrite: the reference's own break; otherwise the break the channel names
                    const int brk = c.brk == QLatin1String("Overwrite") ? r.breaks.begin()->first : c.brk.toInt();
                    const auto b = r.breaks.find(brk);
                    if (brk != 1 || b == r.breaks.end()) {
                        skippedBreak |= brk != 1;
                        continue;
                    }
                    addChannel(t, c, b->second - 1, pixel);
                }
            }
        } else {
            // Each geometry with colour channels (else with a dimmer) is a pixel; the others' channels the whole fixture's
            std::vector<QString> cellGeoms;
            for (const bool color : {true, false}) {
                for (const Channel &c : channels)
                    if ((color ? isColor(c.role) : c.role == ChannelRole::Dimmer) &&
                        std::find(cellGeoms.begin(), cellGeoms.end(), c.geometry) == cellGeoms.end())
                        cellGeoms.push_back(c.geometry);
                if (!cellGeoms.empty()) break;
            }
            const bool cells = cellGeoms.size() >= 2;
            for (const QString &g : cellGeoms) {
                const auto it = geometries.find(g);
                positions.push_back(it != geometries.end() ? translation(it->second.global) : QVector3D());
            }
            if (!cells) positions.assign(1, QVector3D());
            for (const Channel &c : channels) {
                if (!breakOne(c)) continue;
                int pixel = 0;
                if (cells) {
                    const auto it = std::find(cellGeoms.begin(), cellGeoms.end(), c.geometry);
                    pixel = it != cellGeoms.end() ? int(it - cellGeoms.begin()) : -1;
                }
                addChannel(t, c, 0, pixel);
            }
        }
        if (skippedBreak) warn(QStringLiteral("channels on DMX breaks other than 1 are not patched"));
        if (positions.empty()) positions.emplace_back();
        t.cells = layout(positions, &t.cols, &t.rows);
        std::sort(t.dmxMap.begin(), t.dmxMap.end(), [](const FixtureSlot &a, const FixtureSlot &b) { return a.offset < b.offset; });
        QString why;
        if (t.dmxMap.empty()) {
            warn(QStringLiteral("no channel"));
            continue;
        }
        if (!t.valid(&why)) {
            warn(why);
            continue;
        }
        result.types.push_back(std::move(t));
    }
    if (result.types.empty() && result.error.isEmpty()) result.error = QStringLiteral("No usable DMX mode in %1").arg(name);
    return result;
}

Import readArchive(const QByteArray &archive, const QString &fileName)
{
    Import result;
    const QByteArray xml = zip::read(archive, QStringLiteral("description.xml"), &result.error);
    if (xml.isEmpty()) {
        if (result.error.isEmpty()) result.error = QStringLiteral("Empty description.xml");
        return result;
    }
    return readDescription(xml, fileName);
}

Import readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        Import result;
        result.error = f.errorString();
        return result;
    }
    return readArchive(f.readAll(), QFileInfo(path).fileName());
}

} // namespace gdtf
