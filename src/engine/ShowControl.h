#pragma once
// A show control: a value the operator makes — a slider, a switch, a selector, a button, a color — that belongs to the
// show, not to a layer. It is a parameter of the show (control/<key>), so everything that reaches a parameter reaches it:
// an interaction follows it (an Internal source) or drives it (a target), a timeline animates it, OSC reads and sets it
// (/control/<key>), a remote control moves it. One control read by several interactions is how one gesture drives
// several things at once; one driven by several is how several things meet in one.
//
// A 2D slider (Pad) is two numbers moved by one gesture: control/<key>/x and control/<key>/y, a point as any other (a timeline
// draws it as an XY track, an interaction maps an XY input onto it). A Group is not a value: a titled frame that holds
// controls (and other groups) and lays them out in columns, a control taking one column or more. The list is in the order
// the page shows; a control or a group is in the group its `group` says (0: the page itself).
//
// The definitions and the current values are saved with the project. Never in a snapshot: a control is the operator's.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>

struct ShowControl {
    enum class Kind { Slider = 0, Toggle, Choice, Button, Color, Pad, Group, Adsr };
    static constexpr int kKindCount = 8;
    static constexpr int kGridColumns = 12, kGridRowsMax = 400; // the remote page's grid
    quint64 id = 0;
    QString name = QStringLiteral("Control");
    QString key; // its address, control/<key>: derived from the name and unique within the show
    Kind kind = Kind::Slider;
    quint64 group = 0; // the Group that holds it (0: the page)
    int span = 1;      // the columns it takes in its group (1..4)
    // Its place on the remote page (a 12-column grid of square cells; the page is its top-level Group, or the first page for
    // what is not in one): x, y, w, h in cells. w = 0: not placed yet, the page flows it where it fits
    int gx = 0, gy = 0, gw = 0, gh = 0;
    // How the remote page draws it (the tile's own settings): a widget among those of its kind ("": the default one), the curve
    // of its gesture (a Slider: 1 linear, above 1 fine at the low end, below 1 fine at the high end) and its accent color ("#rrggbb")
    QString widget, accent;
    double curve = 1;
    int columns = 2;   // Group: how many columns it lays its controls out in (1..4)
    double min = 0, max = 1, step = 0.01; // Slider, Pad (both axes)
    QString unit;                         // Slider: " %", " Hz", " s"
    double def = 0;                       // Slider, Pad: where a reset goes
    QStringList choices;                  // Choice: its names (also its keys: renaming one renames what was set)
    // Its value: a slider's number, a switch's 0 / 1, a choice's index, a button's 0 / 1 (1 while it is held down)
    double value = 0;
    double value2 = 0; // Pad: Y (value is X)
    std::array<double, 3> rgb{{1, 1, 1}}; // Color
    // Adsr: an envelope's four numbers, control/<key>/attack, decay, sustain (0..1) and release; the times in seconds up to `max`
    std::array<double, 4> adsr{{0.05, 0.2, 0.7, 0.4}};
    static constexpr const char *const kAdsrKeys[4] = {"attack", "decay", "sustain", "release"};

    // What a remote page may ask for: a short word, a color in #rrggbb; anything else is the default
    static QString cleanWidget(const QString &w)
    {
        if (w.size() > 12) return QString();
        for (const QChar ch : w)
            if (!ch.isLetter()) return QString();
        return w.toLower();
    }
    static QString cleanAccent(const QString &a)
    {
        if (a.size() != 7 || a[0] != QLatin1Char('#')) return QString();
        for (int i = 1; i < 7; ++i)
            if (!a[i].isDigit() && !(a[i].toLower() >= QLatin1Char('a') && a[i].toLower() <= QLatin1Char('f'))) return QString();
        return a.toLower();
    }
    static QString kindKey(Kind k)
    {
        static const char *const keys[] = {"slider", "toggle", "choice", "button", "color", "pad", "group", "adsr"};
        return QString::fromLatin1(keys[std::clamp(int(k), 0, kKindCount - 1)]);
    }
    static Kind kindFromKey(const QString &s)
    {
        for (int k = 0; k < kKindCount; ++k)
            if (kindKey(Kind(k)) == s) return Kind(k);
        return Kind::Slider;
    }
    static QString kindName(Kind k)
    {
        static const char *const names[] = {"Slider", "Toggle", "Selector", "Button", "Color", "2D Slider", "Group", "ADSR"};
        return QString::fromLatin1(names[std::clamp(int(k), 0, kKindCount - 1)]);
    }
    // The value kept within what the control takes
    double clamped(double v) const
    {
        if (!std::isfinite(v)) return value;
        switch (kind) {
        case Kind::Slider:
        case Kind::Pad: return std::clamp(v, std::min(min, max), std::max(min, max)); // (a pad's Y: the same range)
        case Kind::Toggle:
        case Kind::Button: return v >= 0.5 ? 1.0 : 0.0;
        case Kind::Choice: return choices.isEmpty() ? 0.0 : std::clamp(std::round(v), 0.0, double(choices.size() - 1));
        case Kind::Color:
        case Kind::Adsr:
        case Kind::Group: return value;
        }
        return value;
    }
    QJsonObject toJson() const
    {
        QJsonObject o{{"id", QString::number(id)}, {"name", name}, {"key", key}, {"kind", kindKey(kind)}};
        if (group) o["group"] = QString::number(group);
        if (span != 1) o["span"] = span;
        if (!widget.isEmpty()) o["widget"] = widget;
        if (!accent.isEmpty()) o["accent"] = accent;
        if (curve != 1) o["curve"] = curve;
        if (gw > 0 && gh > 0) o["layout"] = QJsonObject{{"x", gx}, {"y", gy}, {"w", gw}, {"h", gh}};
        switch (kind) {
        case Kind::Group: o["columns"] = columns; break;
        case Kind::Pad:
            o["min"] = min;
            o["max"] = max;
            o["step"] = step;
            o["default"] = def;
            o["x"] = value;
            o["y"] = value2;
            break;
        case Kind::Slider:
            o["min"] = min;
            o["max"] = max;
            o["step"] = step;
            o["default"] = def;
            o["value"] = value;
            if (!unit.isEmpty()) o["unit"] = unit;
            break;
        case Kind::Toggle: o["value"] = value >= 0.5; break;
        case Kind::Button: break; // never saved down
        case Kind::Choice: {
            QJsonArray list;
            for (const QString &c : choices) list.append(c);
            o["choices"] = list;
            o["value"] = int(value);
            break;
        }
        case Kind::Color: o["rgb"] = QJsonArray{rgb[0], rgb[1], rgb[2]}; break;
        case Kind::Adsr:
            o["max"] = max;
            o["adsr"] = QJsonArray{adsr[0], adsr[1], adsr[2], adsr[3]};
            break;
        }
        return o;
    }
    static ShowControl fromJson(const QJsonObject &o)
    {
        ShowControl c;
        c.id = o.value("id").toString().toULongLong();
        c.name = o.value("name").toString(QStringLiteral("Control"));
        c.key = o.value("key").toString();
        c.kind = kindFromKey(o.value("kind").toString());
        c.group = o.value("group").toString().toULongLong();
        c.span = std::clamp(o.value("span").toInt(1), 1, 4);
        c.widget = cleanWidget(o.value("widget").toString());
        c.accent = cleanAccent(o.value("accent").toString());
        c.curve = std::isfinite(o.value("curve").toDouble(1)) ? std::clamp(o.value("curve").toDouble(1), 0.1, 10.0) : 1.0;
        if (const QJsonObject l = o.value("layout").toObject(); l.value("w").toInt() > 0 && l.value("h").toInt() > 0) {
            c.gw = std::clamp(l.value("w").toInt(), 1, kGridColumns);
            c.gh = std::clamp(l.value("h").toInt(), 1, kGridRowsMax);
            c.gx = std::clamp(l.value("x").toInt(), 0, kGridColumns - c.gw);
            c.gy = std::clamp(l.value("y").toInt(), 0, kGridRowsMax);
        }
        auto finite = [](double v, double fallback) { return std::isfinite(v) ? v : fallback; };
        switch (c.kind) {
        case Kind::Group: c.columns = std::clamp(o.value("columns").toInt(2), 1, 4); break;
        case Kind::Pad:
            c.min = finite(o.value("min").toDouble(0), 0);
            c.max = finite(o.value("max").toDouble(1), 1);
            if (c.max <= c.min) c.max = c.min + 1;
            c.step = std::clamp(finite(o.value("step").toDouble(0.01), 0.01), 1e-6, std::max(1e-6, c.max - c.min));
            c.def = std::clamp(finite(o.value("default").toDouble(c.min), c.min), c.min, c.max);
            c.value = std::clamp(finite(o.value("x").toDouble(c.def), c.def), c.min, c.max);
            c.value2 = std::clamp(finite(o.value("y").toDouble(c.def), c.def), c.min, c.max);
            break;
        case Kind::Slider:
            c.min = finite(o.value("min").toDouble(0), 0);
            c.max = finite(o.value("max").toDouble(1), 1);
            if (c.max <= c.min) c.max = c.min + 1;
            c.step = std::clamp(finite(o.value("step").toDouble(0.01), 0.01), 1e-6, std::max(1e-6, c.max - c.min));
            c.unit = o.value("unit").toString();
            c.def = std::clamp(finite(o.value("default").toDouble(c.min), c.min), c.min, c.max);
            c.value = std::clamp(finite(o.value("value").toDouble(c.def), c.def), c.min, c.max);
            break;
        case Kind::Toggle: c.value = o.value("value").toBool() ? 1.0 : 0.0; break;
        case Kind::Button: break;
        case Kind::Adsr: {
            c.min = 0;
            c.max = std::clamp(finite(o.value("max").toDouble(2), 2), 0.1, 60.0);
            const QJsonArray a = o.value("adsr").toArray();
            static const double fallback[4] = {0.05, 0.2, 0.7, 0.4};
            for (int i = 0; i < 4; ++i) c.adsr[size_t(i)] = std::clamp(finite(a.at(i).toDouble(fallback[i]), fallback[i]), 0.0, i == 2 ? 1.0 : c.max);
            break;
        }
        case Kind::Choice:
            for (const QJsonValue &v : o.value("choices").toArray()) c.choices << v.toString();
            if (c.choices.isEmpty()) c.choices << QStringLiteral("A") << QStringLiteral("B");
            c.value = std::clamp(double(o.value("value").toInt(0)), 0.0, double(c.choices.size() - 1));
            break;
        case Kind::Color: {
            const QJsonArray a = o.value("rgb").toArray();
            for (int i = 0; i < 3; ++i) c.rgb[size_t(i)] = std::clamp(finite(a.at(i).toDouble(1), 1), 0.0, 1.0);
            break;
        }
        }
        return c;
    }
};
