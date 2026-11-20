#include "Isf.h"
#include <QHash>
#include "ImageFile.h"
#include "LineGenerator.h"
#include "Osc.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Text utilities
// ---------------------------------------------------------------------------

static bool readText(const QString &path, QString *out)
{
    if (const QString builtIn = builtInIsfSource(path); !builtIn.isEmpty()) { // a generator of the application
        *out = builtIn;
        return true;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    *out = QString::fromUtf8(f.readAll());
    return true;
}

// Splits the JSON header (first /* */ comment) from the GLSL body.
static bool splitIsf(const QString &src, QString *json, QString *body)
{
    int start = src.indexOf(QLatin1String("/*"));
    if (start < 0) return false;
    int end = src.indexOf(QLatin1String("*/"), start + 2);
    if (end < 0) return false;
    *json = src.mid(start + 2, end - start - 2);
    if (body) *body = src.mid(end + 2);
    return true;
}

static QJsonObject parseLooseJson(QString json, QString *err)
{
    // Many ISF files contain trailing commas: strip them.
    static const QRegularExpression trailing(QStringLiteral(",(\\s*[}\\]])"));
    json.replace(trailing, QStringLiteral("\\1"));
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (err) *err = QStringLiteral("Invalid JSON header: ") + pe.errorString();
        return {};
    }
    return doc.object();
}

static bool isIdentChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }

// Rewrites IMG_PIXEL / IMG_NORM_PIXEL / IMG_THIS_PIXEL / IMG_THIS_NORM_PIXEL / IMG_SIZE
// into standard GLSL calls. Parses parentheses to support nested arguments. Nested deeper than kMaxDepth, the text is
// left as it is (the shader then fails to compile, with its error) rather than overflowing the stack.
static QString rewriteImgFuncs(const QString &s, int depth = 0)
{
    constexpr int kMaxDepth = 64;
    if (depth > kMaxDepth) return s;
    static const QStringList names = {QStringLiteral("IMG_THIS_NORM_PIXEL"), QStringLiteral("IMG_THIS_PIXEL"),
                                      QStringLiteral("IMG_NORM_PIXEL"), QStringLiteral("IMG_PIXEL"),
                                      QStringLiteral("IMG_SIZE")};
    QString out;
    out.reserve(s.size());
    int i = 0;
    while (i < s.size()) {
        int j = s.indexOf(QLatin1String("IMG_"), i);
        if (j < 0) {
            out += s.mid(i);
            break;
        }
        if (j > 0 && isIdentChar(s[j - 1])) {
            out += s.mid(i, j - i + 4);
            i = j + 4;
            continue;
        }
        QString matched;
        for (const QString &n : names) {
            if (s.mid(j, n.size()) == n) {
                int k = j + n.size();
                if (k < s.size() && isIdentChar(s[k])) continue;
                matched = n;
                break;
            }
        }
        if (matched.isEmpty()) {
            out += s.mid(i, j - i + 4);
            i = j + 4;
            continue;
        }
        int k = j + matched.size();
        while (k < s.size() && s[k].isSpace()) ++k;
        if (k >= s.size() || s[k] != QLatin1Char('(')) {
            out += s.mid(i, k - i);
            i = k;
            continue;
        }
        int depth = 0, p = k, argStart = k + 1;
        QStringList args;
        for (; p < s.size(); ++p) {
            QChar c = s[p];
            if (c == QLatin1Char('(')) {
                ++depth;
            } else if (c == QLatin1Char(')')) {
                if (--depth == 0) {
                    args << s.mid(argStart, p - argStart);
                    break;
                }
            } else if (c == QLatin1Char(',') && depth == 1) {
                args << s.mid(argStart, p - argStart);
                argStart = p + 1;
            }
        }
        if (p >= s.size()) {
            out += s.mid(i);
            break;
        }
        for (QString &a : args) a = rewriteImgFuncs(a, depth + 1).trimmed();
        out += s.mid(i, j - i);
        const QString img = args.value(0);
        if (matched == QLatin1String("IMG_SIZE"))
            out += QStringLiteral("_%1_imgSize").arg(img);
        else if (matched.startsWith(QLatin1String("IMG_THIS")))
            out += QStringLiteral("texture(%1, isf_FragNormCoord)").arg(img);
        else if (matched == QLatin1String("IMG_NORM_PIXEL"))
            out += QStringLiteral("texture(%1, %2)").arg(img, args.value(1));
        else
            out += QStringLiteral("texture(%1, (%2) / _%1_imgSize)").arg(img, args.value(1));
        i = p + 1;
    }
    return out;
}

static QString cleanBody(QString body)
{
    static const QRegularExpression version(QStringLiteral("^\\s*#version[^\\n]*$"),
                                            QRegularExpression::MultilineOption);
    static const QRegularExpression normDecl(
        QStringLiteral("(varying|in|out)\\s+(highp\\s+|mediump\\s+)?vec2\\s+(isf|vv)_FragNormCoord\\s*;"));
    body.replace(version, QString());
    body.replace(normDecl, QString());
    return rewriteImgFuncs(body);
}

// ---------------------------------------------------------------------------
// Small expression evaluator for pass WIDTH/HEIGHT ("$WIDTH/2.0", "floor($HEIGHT*0.25)").
// A failure anywhere fails the whole (the size falls back); nested deeper than kMaxDepth, it fails.
// ---------------------------------------------------------------------------
namespace {
struct ExprParser {
    static constexpr int kMaxDepth = 64;
    QString s;
    const std::vector<IsfInput> &inputs;
    int width, height;
    int p = 0;
    int depth = 0;
    bool ok = true;

    double variable(QStringView name)
    {
        // Resolve only names the expression uses. Numeric inputs with the same name
        // still override the built-ins, as they did in the expression's variable map.
        for (auto it = inputs.rbegin(); it != inputs.rend(); ++it) {
            if (it->name != name) continue;
            switch (it->type) {
            case IsfInput::Float: return it->fValue;
            case IsfInput::Long: return it->lValue;
            case IsfInput::Bool:
            case IsfInput::Event: return it->bValue ? 1.0 : 0.0;
            default: break;
            }
        }
        if (name == QLatin1String("WIDTH")) return width;
        if (name == QLatin1String("HEIGHT")) return height;
        ok = false;
        return 0;
    }

    void ws() { while (p < s.size() && s[p].isSpace()) ++p; }
    bool eat(QChar c)
    {
        ws();
        if (p < s.size() && s[p] == c) { ++p; return true; }
        return false;
    }
    double expr()
    {
        double v = term();
        for (;;) {
            if (eat('+')) v += term();
            else if (eat('-')) v -= term();
            else return v;
        }
    }
    double term()
    {
        double v = unary();
        for (;;) {
            if (eat('*')) v *= unary();
            else if (eat('/')) { double d = unary(); v = d != 0 ? v / d : 0; }
            else return v;
        }
    }
    double unary() // every nesting (parentheses, signs, function arguments) goes through here
    {
        if (depth >= kMaxDepth) {
            ok = false;
            return 0;
        }
        ++depth;
        double v;
        if (eat('-')) v = -unary();
        else if (eat('+')) v = unary();
        else v = primary();
        --depth;
        return v;
    }
    double primary()
    {
        ws();
        if (eat('(')) { double v = expr(); if (!eat(')')) ok = false; return v; }
        if (p < s.size() && (s[p].isDigit() || s[p] == '.')) {
            int st = p;
            while (p < s.size() && (s[p].isDigit() || s[p] == '.' || s[p] == 'e' || s[p] == 'E')) ++p;
            bool number = false;
            const double v = s.mid(st, p - st).toDouble(&number);
            if (!number) ok = false; // never clears an earlier failure
            return v;

        }
        bool dollar = eat('$');
        int st = p;
        while (p < s.size() && isIdentChar(s[p])) ++p;
        const QStringView id = QStringView(s).mid(st, p - st);
        if (id.isEmpty()) { ok = false; return 0; }
        if (dollar) return variable(id);
        // function
        if (!eat('(')) { ok = false; return 0; }
        QVector<double> args;
        if (!eat(')')) {
            do { args << expr(); } while (eat(','));
            if (!eat(')')) ok = false;
        }
        auto a = [&](int i) { return i < args.size() ? args[i] : 0.0; };
        auto is = [&](QLatin1String name) { return id.compare(name, Qt::CaseInsensitive) == 0; };
        if (is(QLatin1String("floor"))) return std::floor(a(0));
        if (is(QLatin1String("ceil"))) return std::ceil(a(0));
        if (is(QLatin1String("round"))) return std::round(a(0));
        if (is(QLatin1String("abs"))) return std::fabs(a(0));
        if (is(QLatin1String("sqrt"))) return std::sqrt(a(0));
        if (is(QLatin1String("pow"))) return std::pow(a(0), a(1));
        if (is(QLatin1String("min"))) return std::min(a(0), a(1));
        if (is(QLatin1String("max"))) return std::max(a(0), a(1));
        ok = false;
        return 0;
    }
};
} // namespace

double IsfInstance::evalSize(const QString &e, int w, int h, int fallback) const
{
    if (e.isEmpty() || e.trimmed().isEmpty()) return fallback;
    ExprParser ep{e, m_inputs, w, h};
    double v = ep.expr();
    if (!ep.ok || !std::isfinite(v)) return fallback;
    return v;
}

// ---------------------------------------------------------------------------

QString IsfInput::typeName(Type t)
{
    switch (t) {
    case Event: return "event";
    case Bool: return "bool";
    case Long: return "long";
    case Float: return "float";
    case Point2D: return "point2D";
    case Color: return "color";
    case Image: return "image";
    case Audio: return "audio";
    case AudioFFT: return "audioFFT";
    default: return "unknown";
    }
}

static IsfInput::Type typeFromString(const QString &s)
{
    const QString t = s.toLower();
    if (t == "event") return IsfInput::Event;
    if (t == "bool") return IsfInput::Bool;
    if (t == "long") return IsfInput::Long;
    if (t == "float") return IsfInput::Float;
    if (t == "point2d") return IsfInput::Point2D;
    if (t == "color") return IsfInput::Color;
    if (t == "image") return IsfInput::Image;
    if (t == "audio") return IsfInput::Audio;
    if (t == "audiofft") return IsfInput::AudioFFT;
    return IsfInput::Unknown;
}

static QPointF jsonPoint(const QJsonValue &v, QPointF def = {})
{
    QJsonArray a = v.toArray();
    if (a.size() >= 2) return QPointF(a[0].toDouble(), a[1].toDouble());
    return def;
}

std::vector<std::pair<QString, QString>> IsfInstance::dependencies(const QString &path)
{
    std::vector<std::pair<QString, QString>> out;
    const QFileInfo fi(path);
    const QDir dir = fi.absoluteDir();
    for (const QString &ext : {QStringLiteral("vs"), QStringLiteral("vert")}) {
        const QString vs = fi.completeBaseName() + '.' + ext;
        if (dir.exists(vs)) out.push_back({dir.absoluteFilePath(vs), vs});
    }
    QString src, json;
    if (!readText(fi.absoluteFilePath(), &src) || !splitIsf(src, &json, nullptr)) return out;
    const QJsonObject o = parseLooseJson(json, nullptr);
    auto add = [&](const QString &p) {
        if (p.isEmpty()) return;
        const QString abs = QDir::cleanPath(dir.absoluteFilePath(p));
        if (QFileInfo(abs).isFile()) out.push_back({abs, QDir::cleanPath(p)});
    };
    const QJsonValue imp = o.value("IMPORTED");
    if (imp.isObject()) {
        const QJsonObject io = imp.toObject();
        for (auto it = io.begin(); it != io.end(); ++it) add(it.value().toObject().value("PATH").toString());
    } else if (imp.isArray()) {
        for (const QJsonValue &x : imp.toArray()) add(x.toObject().value("PATH").toString());
    }
    return out;
}

IsfInstance::Header IsfInstance::readHeader(const QString &path)
{
    Header h;
    QString src, json;
    if (!readText(path, &src) || !splitIsf(src, &json, nullptr)) return h;
    QJsonObject o = parseLooseJson(json, nullptr);
    if (o.isEmpty()) return h;
    h.ok = true;
    h.name = QFileInfo(path).completeBaseName();
    h.description = o.value("DESCRIPTION").toString();
    for (const QJsonValue &c : o.value("CATEGORIES").toArray()) h.categories << c.toString();
    bool start = false, end = false;
    for (const QJsonValue &v : o.value("INPUTS").toArray()) {
        QJsonObject in = v.toObject();
        const QString n = in.value("NAME").toString();
        if (in.value("TYPE").toString().toLower() != "image") continue;
        if (n == "inputImage") h.isFilter = true;
        if (n == "startImage") start = true;
        if (n == "endImage") end = true;
    }
    h.isTransition = start && end;
    return h;
}

std::vector<Parameter> IsfInstance::parameters(const QString &title) const
{
    using T = Parameter::Type;
    auto *self = const_cast<IsfInstance *>(this); // the parameters write into it
    std::vector<Parameter> out;
    const QString cat = title + QStringLiteral(" › ");
    out.emplace_back(QStringLiteral("speed"), cat + QStringLiteral("Speed"), T::Float);
    out.back().range(0, 10).step(0.01).byDefault(1.0).bind([self] { return self->speed; },
                                                           [self](const QVariant &v) { self->speed = v.toDouble(); });
    // Rewind: from its first frame, as when it was loaded (an action: a preset or a cue can hold it)
    out.emplace_back(QStringLiteral("rewind"), cat + QStringLiteral("Rewind"), T::Trigger);
    out.back().bind(nullptr, [self](const QVariant &) { self->requestRestart(); });
    out.emplace_back(QStringLiteral("enable"), cat + QStringLiteral("Enable"), T::Bool);
    out.back().byDefault(true).bind([self] { return self->enabled; }, [self](const QVariant &v) { self->enabled = v.toBool(); });
    // How much of the effect goes on: a fade between the picture it was given and its result (under its mask and channels)
    out.emplace_back(QStringLiteral("mix"), cat + QStringLiteral("Mix"), T::Float);
    out.back().range(0, 1).byDefault(1.0).bind([self] { return self->mix; },
                                                [self](const QVariant &v) { self->mix = std::clamp(v.toDouble(), 0.0, 1.0); });
    out.emplace_back(QStringLiteral("mask/invert"), cat + QStringLiteral("Invert Mask"), T::Bool);
    out.back().byDefault(false).bind([self] { return self->maskInvert; }, [self](const QVariant &v) { self->maskInvert = v.toBool(); });
    out.emplace_back(QStringLiteral("mask_only"), cat + QStringLiteral("Mask Only"), T::Bool);
    out.back().byDefault(false).ramp(Parameter::Ramp::Cut).bind([self] { return self->maskOnly; },
                                                                [self](const QVariant &v) { self->maskOnly = v.toBool(); });
    // The channels an effect changes: each one on or off (off: it keeps the picture the effect was given)
    for (int c = 0; c < 4; ++c) {
        out.emplace_back(QStringLiteral("channel/") + QChar("rgba"[c]), cat + QStringLiteral("Channel ") + QChar("RGBA"[c]), T::Bool);
        out.back().byDefault(true).ramp(Parameter::Ramp::Cut).bind(
            [self, c] { return self->channels[size_t(c)]; }, [self, c](const QVariant &v) { self->channels[size_t(c)] = v.toBool(); });
    }
    for (const IsfInput &in : m_inputs) {
        if (in.isInputImage) continue;
        const QString name = in.name;
        const QString path = QStringLiteral("param/") + osc::safeName(name);
        const QString label = cat + (in.label.isEmpty() ? name : in.label);
        // The input, found again by its name
        auto input = [self, name]() -> IsfInput * { return self->input(name); };
        // One number of its value (k: the component of a point or a color, -1: the value)
        auto number = [&](const QString &p, const QString &l, int k, double lo, double hi, double def) {
            out.emplace_back(p, l, T::Float);
            const IsfInput::Type type = in.type;
            out.back().range(lo, hi).byDefault(def).bind(
                [input, k, type]() -> QVariant {
                    const IsfInput *x = input();
                    if (!x) return {};
                    if (type == IsfInput::Point2D) return k == 0 ? x->pValue.x() : x->pValue.y();
                    if (type == IsfInput::Color) return double(x->cValue[k]);
                    return x->fValue;
                },
                [input, k, type](const QVariant &v) {
                    IsfInput *x = input();
                    if (!x) return;
                    IsfValue val = x->value();
                    if (type == IsfInput::Point2D) (k == 0 ? val.p.rx() : val.p.ry()) = v.toDouble();
                    else if (type == IsfInput::Color) val.c[k] = float(v.toDouble());
                    else val.f = v.toDouble();
                    x->setValue(val);
                });
            return &out.back();
        };
        switch (in.type) {
        case IsfInput::Float:
            number(path, label, -1, in.fMin, in.fMax, in.fDefault)->step(std::abs(in.fMax - in.fMin) / 1000.0);
            break;
        case IsfInput::Long: // one among its values (any whole number when it lists none): at once in a fade
            out.emplace_back(path, label, T::Int);
            if (in.lValues.isEmpty()) out.back().range(0, 100).limits(-1e6, 1e6);
            else out.back().values(QList<int>(in.lValues.begin(), in.lValues.end()));
            out.back().ramp(Parameter::Ramp::Cut).byDefault(in.lDefault).bind(
                [input]() -> QVariant { const IsfInput *x = input(); return x ? QVariant(x->lValue) : QVariant(); },
                [input](const QVariant &v) {
                    if (IsfInput *x = input()) {
                        IsfValue val = x->value();
                        val.l = v.toInt();
                        x->setValue(val);
                    }
                });
            break;
        case IsfInput::Point2D: {
            const QPointF lo = in.hasPointRange ? in.pMin : QPointF(0, 0), hi = in.hasPointRange ? in.pMax : QPointF(1, 1);
            number(path + "/x", label + " X", 0, lo.x(), hi.x(), in.pDefault.x())->limits(-1e6, 1e6).pointComponent(0);
            number(path + "/y", label + " Y", 1, lo.y(), hi.y(), in.pDefault.y())->limits(-1e6, 1e6).pointComponent(1);
            break;
        }
        case IsfInput::Color:
            for (int c = 0; c < 4; ++c)
                number(path + "/" + QChar("rgba"[c]), label + " " + QChar("RGBA"[c]), c, 0, 1, in.cDefault[c])->colorComponent(c);
            break;
        case IsfInput::Bool:
            out.emplace_back(path, label, T::Bool);
            out.back().byDefault(in.bDefault).bind(
                [input]() -> QVariant { const IsfInput *x = input(); return x ? QVariant(x->bValue) : QVariant(); },
                [input](const QVariant &v) {
                    if (IsfInput *x = input()) x->bValue = v.toBool();
                });
            break;
        case IsfInput::Event:
            out.emplace_back(path, label, T::Trigger);
            out.back().bind(nullptr, [input](const QVariant &) {
                if (IsfInput *x = input()) x->eventFired = true;
            });
            break;
        default: break; // images and sound: not parameters
        }
    }
    return out;
}

IsfInput *IsfInstance::input(const QString &name)
{
    for (IsfInput &in : m_inputs)
        if (in.name == name) return &in;
    return nullptr;
}

IsfInstance::~IsfInstance() = default;

bool IsfInstance::parse(const QString &src, QString *body)
{
    QString json;
    if (!splitIsf(src, &json, body)) {
        m_error = QStringLiteral("No ISF header (missing /* */ JSON comment).");
        return false;
    }
    QJsonObject o = parseLooseJson(json, &m_error);
    if (o.isEmpty()) return false;

    m_description = o.value("DESCRIPTION").toString();
    m_credit = o.value("CREDIT").toString();

    for (const QJsonValue &v : o.value("INPUTS").toArray()) {
        QJsonObject j = v.toObject();
        IsfInput in;
        in.name = j.value("NAME").toString();
        if (in.name.isEmpty()) continue;
        in.label = j.value("LABEL").toString(in.name);
        in.type = typeFromString(j.value("TYPE").toString());
        switch (in.type) {
        case IsfInput::Float:
            in.fMin = j.value("MIN").toDouble(0.0);
            in.fMax = j.value("MAX").toDouble(1.0);
            if (in.fMax <= in.fMin) in.fMax = in.fMin + 1.0;
            in.fDefault = j.value("DEFAULT").toDouble(in.fMin);
            in.fValue = in.fDefault;
            break;
        case IsfInput::Audio:
        case IsfInput::AudioFFT: in.audioMax = std::clamp(j.value("MAX").toInt(0), 0, 4096); break;
        case IsfInput::Bool:
        case IsfInput::Event: {
            QJsonValue d = j.value("DEFAULT");
            in.bDefault = d.isBool() ? d.toBool() : d.toDouble() != 0.0;
            in.bValue = in.type == IsfInput::Event ? false : in.bDefault;
            break;
        }
        case IsfInput::Long: {
            for (const QJsonValue &x : j.value("VALUES").toArray()) in.lValues << x.toInt();
            for (const QJsonValue &x : j.value("LABELS").toArray()) in.lLabels << x.toVariant().toString();
            if (in.lValues.isEmpty()) {
                int mn = j.value("MIN").toInt(0), mx = j.value("MAX").toInt(mn + 10);
                for (int k = mn; k <= mx && in.lValues.size() < 512; ++k) in.lValues << k;
            }
            while (in.lLabels.size() < in.lValues.size()) in.lLabels << QString::number(in.lValues[in.lLabels.size()]);
            in.lDefault = j.value("DEFAULT").toInt(in.lValues.value(0));
            in.lValue = in.lDefault;
            break;
        }
        case IsfInput::Point2D:
            in.pDefault = jsonPoint(j.value("DEFAULT"));
            in.pValue = in.pDefault;
            if (j.contains("MIN") && j.contains("MAX")) {
                in.hasPointRange = true;
                in.pMin = jsonPoint(j.value("MIN"));
                in.pMax = jsonPoint(j.value("MAX"), QPointF(1, 1));
            }
            break;
        case IsfInput::Color: {
            QJsonArray a = j.value("DEFAULT").toArray();
            for (int k = 0; k < 4; ++k) in.cDefault[k] = k < a.size() ? float(a[k].toDouble()) : (k == 3 ? 1.f : 0.f);
            std::copy(in.cDefault, in.cDefault + 4, in.cValue);
            break;
        }
        case IsfInput::Image:
            in.isInputImage = in.name == QLatin1String("inputImage");
            if (in.isInputImage) m_isFilter = true;
            in.layerOnly = !in.isInputImage && j.value("LAYER_ONLY").toBool();
            break;
        default: break;
        }
        m_inputs.push_back(std::move(in));
    }

    for (const QJsonValue &v : o.value("PASSES").toArray()) {
        QJsonObject j = v.toObject();
        IsfPass p;
        p.target = j.value("TARGET").toString();
        p.persistent = j.value("PERSISTENT").toVariant().toBool();
        p.isFloat = j.value("FLOAT").toVariant().toBool();
        auto expr = [](const QJsonValue &x) { return x.isDouble() ? QString::number(x.toDouble()) : x.toString(); };
        p.widthExpr = expr(j.value("WIDTH"));
        p.heightExpr = expr(j.value("HEIGHT"));
        p.pointsExpr = expr(j.value("POINTS"));
        p.alphaBlend = j.value("BLEND").toString().compare("ALPHA", Qt::CaseInsensitive) == 0;
        p.clear = j.value("CLEAR").toVariant().toBool() || !j.contains("CLEAR");
        m_passes.push_back(p);
        if (!p.target.isEmpty()) {
            auto it = std::find_if(m_targets.begin(), m_targets.end(), [&](const IsfTarget &t) { return t.name == p.target; });
            if (it == m_targets.end()) {
                IsfTarget t;
                t.name = p.target;
                t.persistent = p.persistent;
                t.isFloat = p.isFloat;
                m_targets.push_back(std::move(t));
            } else {
                it->persistent |= p.persistent;
                it->isFloat |= p.isFloat;
            }
        }
    }
    // ISF v1: "PERSISTENT_BUFFERS"
    QJsonValue pb = o.value("PERSISTENT_BUFFERS");
    QStringList persistentNames;
    if (pb.isArray()) for (const QJsonValue &x : pb.toArray()) persistentNames << x.toString();
    else if (pb.isObject()) persistentNames = pb.toObject().keys();
    for (IsfTarget &t : m_targets) if (persistentNames.contains(t.name)) t.persistent = true;

    // Imported images: object {name: {PATH}} or array [{NAME, PATH}]
    QJsonValue imp = o.value("IMPORTED");
    const QDir dir = QFileInfo(m_path).absoluteDir();
    auto addImport = [&](const QString &n, const QString &p) {
        if (n.isEmpty() || p.isEmpty()) return;
        IsfImported im;
        im.name = n;
        im.path = dir.absoluteFilePath(p);
        m_imported.push_back(std::move(im));
    };
    if (imp.isObject()) {
        QJsonObject io = imp.toObject();
        for (auto it = io.begin(); it != io.end(); ++it) addImport(it.key(), it.value().toObject().value("PATH").toString());
    } else if (imp.isArray()) {
        for (const QJsonValue &x : imp.toArray())
            addImport(x.toObject().value("NAME").toString(), x.toObject().value("PATH").toString());
    }
    return true;
}

// The .fs file parsed (its inputs, passes, targets, imports) and its bodies cleaned, with its vertex shader if any
// (.vs, .vert): what compile() builds its programs from.
bool IsfInstance::readSources(const QString &path, QString *fragment, QString *vertex)
{
    m_path = QFileInfo(path).absoluteFilePath();
    m_name = QFileInfo(path).completeBaseName();
    QString src;
    if (!readText(m_path, &src)) {
        m_error = QStringLiteral("File not found: ") + path;
        return false;
    }
    QString body;
    if (!parse(src, &body)) return false;
    vertex->clear();
    QFileInfo fi(m_path);
    for (const QString &ext : {QStringLiteral("vs"), QStringLiteral("vert")}) {
        QString vsPath = fi.absolutePath() + "/" + fi.completeBaseName() + "." + ext;
        if (QFile::exists(vsPath) && readText(vsPath, vertex)) {
            *vertex = cleanBody(*vertex);
            break;
        }
    }
    *fragment = cleanBody(body);
    return true;
}

// The varyings a body declares itself (varying / in / out at the start of a line, ended by ';') put at explicit
// locations: the same name, the same location in both stages; 0 and 1 are the host's (isf_FragNormCoord,
// isf_PointCoord). Fragment outputs are left alone.
static QString locateVaryings(const QString &body, bool vertex, QHash<QString, int> &locations)
{
    static const QRegularExpression decl(QStringLiteral(
        R"(^([ \t]*)(varying|in|out)\s+((?:(?:flat|smooth|noperspective|highp|mediump|lowp)\s+)*)(\w+)\s+(\w+)(\s*\[[^\]]*\])?\s*;)"),
        QRegularExpression::MultilineOption);
    QString out;
    qsizetype last = 0;
    for (auto it = decl.globalMatch(body); it.hasNext();) {
        const auto m = it.next();
        const QString qualifier = m.captured(2);
        if (!vertex && qualifier == QLatin1String("out")) continue; // a fragment output
        if (vertex && qualifier == QLatin1String("in")) continue;   // a vertex attribute
        const QString name = m.captured(5);
        if (!locations.contains(name)) locations.insert(name, 2 + int(locations.size()));
        const QString stage = qualifier == QLatin1String("varying") ? (vertex ? QStringLiteral("out") : QStringLiteral("in")) : qualifier;
        out += body.mid(last, m.capturedStart() - last);
        out += QStringLiteral("%1layout(location = %2) %3 %4%5 %6%7;")
                   .arg(m.captured(1)).arg(locations.value(name)).arg(stage, m.captured(3), m.captured(4), name, m.captured(6));
        last = m.capturedEnd();
    }
    out += body.mid(last);
    return out;
}

IsfInstance::RhiSources IsfInstance::rhiSources(const QString &path)
{
    IsfInstance isf;
    QString fsBody, vsBody;
    if (!isf.readSources(path, &fsBody, &vsBody)) {
        RhiSources r;
        r.error = isf.m_error;
        return r;
    }
    return isf.rhiSourcesOf(fsBody, vsBody);
}

IsfInstance::RhiSources IsfInstance::rhiSourcesOf(const QString &fsBody, const QString &vsBody) const
{
    RhiSources r;
    const IsfInstance &isf = *this;
    QString block = QStringLiteral("layout(std140, binding = 0) uniform IsfUniforms {\n");
    using U = RhiSources::Uniform;
    auto member = [&](U::Kind kind, const QString &name, std::initializer_list<float> value = {}) {
        static const char *const glsl[] = {"int", "float", "bool", "vec2", "vec4"};
        const int align = kind == U::Vec4 ? 16 : kind == U::Vec2 ? 8 : 4, size = align; // std140
        U u;
        u.kind = kind;
        u.name = name;
        u.offset = (r.blockSize + align - 1) / align * align;
        std::copy(value.begin(), value.end(), u.value);
        r.blockSize = u.offset + size;
        r.uniforms.push_back(u);
        block += QStringLiteral("    %1 %2;\n").arg(QLatin1String(glsl[kind]), name);
    };
    member(U::Int, QStringLiteral("PASSINDEX"));
    member(U::Vec2, QStringLiteral("RENDERSIZE"));
    member(U::Float, QStringLiteral("TIME"));
    member(U::Float, QStringLiteral("TIMEDELTA"));
    member(U::Vec4, QStringLiteral("DATE"));
    member(U::Int, QStringLiteral("FRAMEINDEX"));
    // -1 where the framebuffer's first row is the top of NDC (Metal, Direct3D): drawn upside down, the first row
    // holds the picture's bottom first, so the shaders sample their images as ISF defines it
    member(U::Float, QStringLiteral("isf_ndcFlip"), {1});
    QString samplers;
    auto sampler = [&](const QString &n) {
        r.samplers << n;
        samplers += QStringLiteral("layout(binding = %2) uniform sampler2D %1;\n").arg(n).arg(r.samplers.size());
        member(U::Vec2, QStringLiteral("_%1_imgSize").arg(n));
        member(U::Vec4, QStringLiteral("_%1_imgRect").arg(n));
        member(U::Bool, QStringLiteral("_%1_flip").arg(n));
    };
    for (const IsfInput &in : isf.m_inputs) {
        switch (in.type) {
        case IsfInput::Event: member(U::Bool, in.name); break;
        case IsfInput::Bool: member(U::Bool, in.name, {in.bDefault ? 1.0f : 0.0f}); break;
        case IsfInput::Long: member(U::Int, in.name, {float(in.lDefault)}); break;
        case IsfInput::Float: member(U::Float, in.name, {float(in.fDefault)}); break;
        case IsfInput::Point2D: member(U::Vec2, in.name, {float(in.pDefault.x()), float(in.pDefault.y())}); break;
        case IsfInput::Color: member(U::Vec4, in.name, {in.cDefault[0], in.cDefault[1], in.cDefault[2], in.cDefault[3]}); break;
        case IsfInput::Image:
        case IsfInput::Audio:
        case IsfInput::AudioFFT:
            if (in.isInputImage) r.inputImage = in.name;
            sampler(in.name);
            break;
        default: break;
        }
    }
    for (const IsfTarget &t : isf.m_targets) sampler(t.name);
    for (const IsfImported &im : isf.m_imported) sampler(im.name);
    // A sampler the shader declares itself, fed by the engine (the Line generator's segments): declared here, with a binding
    static const QRegularExpression ownSampler(QStringLiteral(R"(\buniform\s+sampler2D\s+nyxmapSegments\s*;)"));
    QString fsText = fsBody;
    if (fsText.contains(ownSampler)) {
        fsText.replace(ownSampler, QString());
        sampler(QStringLiteral("nyxmapSegments"));
    }
    r.blockSize = (r.blockSize + 15) / 16 * 16;
    block += QStringLiteral("};\n");
    const QString uniforms = block + samplers;

    QHash<QString, int> locations;
    const QString vertexBody = locateVaryings(vsBody, true, locations);
    const QString fragmentBody = locateVaryings(fsText, false, locations);
    // A built-in function the shader defines again (float round(float x) { ... }): a GLSL compiler takes it as an overload,
    // Vulkan's compiler refuses its precision. Renamed, the shader's own is the one it calls.
    static const QRegularExpression redefined(QStringLiteral(
        R"(\b(?:float|vec[234]|int)\s+(round|trunc|roundEven|fract|sign|mod|mix|clamp|step|smoothstep|saturate)\s*\()"));
    QStringList renamed;
    for (auto it = redefined.globalMatch(fsBody); it.hasNext();) {
        const QString name = it.next().captured(1);
        if (!renamed.contains(name)) renamed << name;
    }
    QString renames;
    for (const QString &name : renamed) renames += QStringLiteral("#define %1 isf_%1\n").arg(name);

    r.fragment = QStringLiteral("#version 440\n"
                                "layout(location = 0) in vec2 isf_FragNormCoord;\n"
                                "layout(location = 1) in vec2 isf_PointCoord;\n"
                                "layout(location = 0) out vec4 isf_FragColor;\n"
                                "#define gl_FragColor isf_FragColor\n"
                                // `signed` is legal GLSL but reserved in Metal Shading Language.
                                "#define signed isf_signed\n"
                                "#define texture2D texture\n"
                                "#define texture2DRect texture\n"
                                "#define vv_FragNormCoord isf_FragNormCoord\n"
                                "#define gl_PointCoord isf_PointCoord\n")
                 + uniforms + renames + QStringLiteral("#line 1\n") + fragmentBody;
    r.vertex = QStringLiteral("#version 440\n"
                              "layout(location = 0) in vec2 isf_position;\n"
                              "layout(location = 0) out vec2 isf_FragNormCoord;\n"
                              "layout(location = 1) out vec2 isf_PointCoord;\n"
                              "#define gl_VertexID gl_VertexIndex\n"
                              "#define signed isf_signed\n")
               + uniforms
               + QStringLiteral("int isf_PointIndex() { return gl_VertexID / 6; }\n"
                                "vec4 isf_PointSprite(vec4 centre, float size) {\n"
                                "    const vec2 corners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(-1, 1), vec2(-1, 1), vec2(1, -1), vec2(1, 1));\n"
                                "    vec2 k = corners[gl_VertexID % 6];\n"
                                "    isf_PointCoord = k * 0.5 + 0.5;\n"
                                "    return centre + vec4(k * size / RENDERSIZE * centre.w, 0.0, 0.0);\n"
                                "}\n"
                                "void isf_vertShaderInit() {\n"
                                "    gl_Position = vec4(isf_position.x, isf_position.y * isf_ndcFlip, 0.0, 1.0);\n"
                                "    isf_FragNormCoord = isf_position * 0.5 + 0.5;\n"
                                "}\n"
                                "#define vv_vertShaderInit isf_vertShaderInit\n"
                                "#define vv_FragNormCoord isf_FragNormCoord\n"
                                "#define texture2D texture\n")
               + (vertexBody.isEmpty() ? QStringLiteral("void main() { isf_vertShaderInit(); }\n")
                                       : QStringLiteral("#line 1\n") + vertexBody);
    r.vertexPoints = r.vertex;
    r.vertexPoints.replace(QStringLiteral("layout(location = 0) in vec2 isf_position;\n"),
                           QStringLiteral("const vec2 isf_position = vec2(0.0);\n"));
    return r;
}

bool IsfInstance::load(const QString &path)
{
    m_inputs.clear();
    m_passes.clear();
    m_targets.clear();
    m_imported.clear();
    m_error.clear();
    m_valid = false;
    m_isFilter = false;
    m_canSkipDraw = false;
    m_usesTime = false;
    m_time = 0;
    m_frameIndex = 0;
    QString fragment, vsBody;
    if (!readSources(path, &fragment, &vsBody)) return false;
    // The shaders are drawn by the QRhi renderer (loadRhi, with its own instance); this one holds what the file says, and
    // has seen that they compile
    {
        QString why;
        if (!checkShaders(rhiSourcesOf(fragment, vsBody), &why)) {
            m_error = why;
            return false;
        }
    }
    // A fullscreen pass with no intermediate buffers or discard cannot read
    // or retain pixels from the previous frame. Custom vertex shaders can.
    static const QRegularExpression discard(QStringLiteral("\\bdiscard\\b"));
    static const QRegularExpression timeUse(QStringLiteral("\\bTIME(DELTA)?\\b"));
    m_canSkipDraw = vsBody.isEmpty() && m_targets.empty() &&
                    (m_passes.empty() || (m_passes.size() == 1 && m_passes.front().pointsExpr.isEmpty())) &&
                    !fragment.contains(discard);
    m_usesTime = fragment.contains(timeUse) || vsBody.contains(timeUse);
    m_valid = true;
    return true;
}

bool IsfInstance::reloadSafely()
{
    // A new shader must never replace the live one before it is valid: read in a separate instance, then exchanged
    IsfInstance prepared;
    if (!prepared.load(m_path)) {
        m_error = prepared.error();
        return false;
    }
    // Preserve user controls and current playback time. The shader may add
    // or remove inputs; restore matches by their parameter path/name.
    const QJsonObject state = save(QString());
    prepared.restore(state, QString());
    prepared.m_time = m_time;
    prepared.m_frameIndex = m_frameIndex;
    for (IsfInput &in : prepared.m_inputs) {
        if (IsfInput *old = input(in.name)) {
            in.rhiExt = old->rhiExt;
            in.extW = old->extW;
            in.extH = old->extH;
        }
    }
    using std::swap;
    swap(m_path, prepared.m_path);
    swap(m_name, prepared.m_name);
    swap(m_description, prepared.m_description);
    swap(m_credit, prepared.m_credit);
    swap(m_error, prepared.m_error);
    swap(m_isFilter, prepared.m_isFilter);
    swap(m_canSkipDraw, prepared.m_canSkipDraw);
    swap(m_usesTime, prepared.m_usesTime);
    swap(m_valid, prepared.m_valid);
    swap(m_inputs, prepared.m_inputs);
    swap(m_passes, prepared.m_passes);
    swap(m_targets, prepared.m_targets);
    swap(m_imported, prepared.m_imported);
    swap(m_time, prepared.m_time);
    swap(m_frameIndex, prepared.m_frameIndex);
    m_rhi.reset(); // the renderer's own instance follows the new file
    return true;
}

bool IsfInstance::setImageInput(int index, const QString &path, QString *err)
{
    if (index < 0 || index >= int(m_inputs.size())) return false;
    IsfInput &in = m_inputs[size_t(index)];
    if (path.isEmpty()) {
        in.imagePath.clear();
        return true;
    }
    if (in.layerOnly) {
        if (err) *err = QStringLiteral("%1 shows a layer, not an image file.").arg(in.label);
        return false;
    }
    if (!QImageReader(path).canRead()) {
        if (err) *err = QStringLiteral("Cannot read the image %1.").arg(path);
        return false;
    }
    in.imagePath = QFileInfo(path).absoluteFilePath();
    return true;
}

void IsfInstance::advanceWithoutDrawing(const IsfRenderContext &rc)
{
    m_time += (rc.elapsed >= 0 ? rc.elapsed : rc.dt) * speed;
    for (IsfInput &in : m_inputs) in.eventFired = false;
    ++m_frameIndex;
}

// ---------------------------------------------------------------------------
// Parameter saving
// ---------------------------------------------------------------------------

QJsonObject IsfInstance::save(const QString &projectDir, bool withParams) const
{
    QJsonObject o;
    o["path"] = m_path;
    if (!projectDir.isEmpty() && builtInIsfSource(m_path).isEmpty()) o["relative_path"] = QDir(projectDir).relativeFilePath(m_path);
    if (maskLayer) {
        o["mask"] = QString::number(maskLayer); // ids are strings: JSON numbers are doubles
        o["mask_stage"] = maskStage;
    }
    QJsonObject images;
    for (const IsfInput &in : m_inputs)
        if (in.type == IsfInput::Image && !in.isInputImage && !in.imagePath.isEmpty()) images[in.name] = in.imagePath;
    if (!images.isEmpty()) o["images"] = images;
    QJsonObject imageLayers;
    for (const IsfInput &in : m_inputs)
        if (in.type == IsfInput::Image && !in.isInputImage && in.imageLayer)
            imageLayers[in.name] = QString::number(in.imageLayer); // ids are strings: JSON numbers are doubles
    if (!imageLayers.isEmpty()) o["image_layers"] = imageLayers;
    QJsonObject audio; // the sound of its audio inputs, when not the live input
    for (const IsfInput &in : m_inputs)
        if ((in.type == IsfInput::Audio || in.type == IsfInput::AudioFFT) && in.audioSource == IsfInput::Program)
            audio[in.name] = QStringLiteral("program");
    if (!audio.isEmpty()) o["audio_sources"] = audio;
    if (withParams) {
        std::vector<Parameter> params = parameters(QString());
        std::vector<Parameter *> list;
        for (Parameter &p : params) list.push_back(&p);
        o["params"] = parametersToJson(list);
    }
    return o;
}

void IsfInstance::restore(const QJsonObject &o, const QString &projectDir)
{
    maskLayer = o.value("mask").toString().toULongLong();
    maskStage = o.value("mask_stage").toInt(-1);
    const QJsonObject images = o.value("images").toObject();
    for (int idx = 0; idx < int(m_inputs.size()); ++idx) {
        IsfInput &in = m_inputs[size_t(idx)];
        if (in.type != IsfInput::Image || in.layerOnly || !images.contains(in.name)) continue;
        QString p = images.value(in.name).toString();
        if (!QFile::exists(p) && !projectDir.isEmpty()) {
            const QString alt = QDir(projectDir).absoluteFilePath(QFileInfo(p).fileName());
            if (QFile::exists(alt)) p = alt;
        }
        if (QFile::exists(p)) setImageInput(idx, p, nullptr);
        else if (!p.isEmpty()) in.imagePath = p; // kept (missing) for the media bin and saving
    }
    const QJsonObject imageLayers = o.value("image_layers").toObject();
    for (IsfInput &in : m_inputs)
        if (in.type == IsfInput::Image && !in.isInputImage) in.imageLayer = imageLayers.value(in.name).toString().toULongLong();
    const QJsonObject audio = o.value("audio_sources").toObject();
    for (IsfInput &in : m_inputs)
        if (in.type == IsfInput::Audio || in.type == IsfInput::AudioFFT)
            in.audioSource = audio.value(in.name).toString() == QLatin1String("program") ? IsfInput::Program : IsfInput::LiveInput;
    if (o.contains("params")) {
        std::vector<Parameter> params = parameters(QString());
        std::vector<Parameter *> list;
        for (Parameter &p : params) list.push_back(&p);
        parametersFromJson(list, o.value("params").toObject());
    }
}

void IsfInstance::resetParams()
{
    for (IsfInput &in : m_inputs) {
        in.fValue = in.fDefault;
        in.bValue = in.bDefault;
        in.lValue = in.lDefault;
        in.pValue = in.pDefault;
        std::copy(in.cDefault, in.cDefault + 4, in.cValue);
    }
}
