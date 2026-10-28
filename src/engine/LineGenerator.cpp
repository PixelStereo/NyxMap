#include "LineGenerator.h"

#include <QJsonObject>
#include <algorithm>
#include <cmath>

// Each pixel finds the nearest segment (pinolSegments: lineSegmentData), in pixels: within half the thickness it is on
// the line, at a place along it (u, 0..1 from its start to its end: of the whole line, or of its segment) and across
// it (v, 0..1). The animation lights it there; the joins are round, the edges smoothed over a pixel.
static const char *kLineShader = R"ISF(/*{
    "DESCRIPTION": "Lines drawn on the layer, animated along them: fill, chase, strobe, pulse, or a media laid along them",
    "CREDIT": "Pinol",
    "ISFVSN": "2",
    "CATEGORIES": ["Generator"],
    "INPUTS": [
        {"NAME": "animation", "LABEL": "Animation", "TYPE": "long", "VALUES": [0, 1, 2, 3, 4, 5], "LABELS": ["Solid", "Fill", "Chase", "Strobe", "Pulse", "Media"], "DEFAULT": 2},
        {"NAME": "lineColor", "LABEL": "Color", "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0]},
        {"NAME": "gradient", "LABEL": "Gradient", "TYPE": "bool", "DEFAULT": false},
        {"NAME": "endColor", "LABEL": "End Color", "TYPE": "color", "DEFAULT": [0.0, 0.5, 1.0, 1.0]},
        {"NAME": "backColor", "LABEL": "Background", "TYPE": "color", "DEFAULT": [0.0, 0.0, 0.0, 0.0]},
        {"NAME": "thickness", "LABEL": "Thickness", "TYPE": "float", "MIN": 1.0, "MAX": 200.0, "DEFAULT": 8.0},
        {"NAME": "softness", "LABEL": "Softness", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.0},
        {"NAME": "rate", "LABEL": "Rate", "TYPE": "float", "MIN": -10.0, "MAX": 10.0, "DEFAULT": 0.5},
        {"NAME": "fill", "LABEL": "Fill", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 1.0},
        {"NAME": "dashes", "LABEL": "Dashes", "TYPE": "float", "MIN": 1.0, "MAX": 64.0, "DEFAULT": 4.0},
        {"NAME": "dashWidth", "LABEL": "Dash Width", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.5},
        {"NAME": "duty", "LABEL": "Duty", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.5},
        {"NAME": "along", "LABEL": "Along", "TYPE": "long", "VALUES": [0, 1], "LABELS": ["Whole Line", "Each Segment"], "DEFAULT": 0},
        {"NAME": "reverse", "LABEL": "Reverse", "TYPE": "bool", "DEFAULT": false},
        {"NAME": "cascade", "LABEL": "Offset", "TYPE": "float", "MIN": 0.0, "MAX": 1.0, "DEFAULT": 0.0},
        {"NAME": "cascadeBy", "LABEL": "Offset By", "TYPE": "long", "VALUES": [0, 1], "LABELS": ["Line", "Segment"], "DEFAULT": 0},
        {"NAME": "media", "LABEL": "Media", "TYPE": "image"}
    ]
}*/

uniform sampler2D pinolSegments;

// 1 on a band of width w (of a unit) centred on each whole x, 0 off it, its edges smoothed over a pixel
float band(float x, float w)
{
    if (w <= 0.0) return 0.0;
    if (w >= 1.0) return 1.0;
    float aa = max(fwidth(x) * 2.0, 1e-5);
    float d = abs(fract(x + 0.5) - 0.5) * 2.0;
    return 1.0 - smoothstep(w - aa, w + aa, d);
}

void main()
{
    vec2 px = vec2(isf_FragNormCoord.x, 1.0 - isf_FragNormCoord.y) * RENDERSIZE; // the frame, y down, in pixels
    int count = textureSize(pinolSegments, 0).x;
    float best = 1e20, side = 0.0, u = 0.0, index = 0.0;
    for (int i = 0; i < count; ++i) {
        vec4 s = texelFetch(pinolSegments, ivec2(i, 0), 0);
        vec4 m = texelFetch(pinolSegments, ivec2(i, 1), 0);
        if (m.z < 0.0) continue; // none
        vec2 a = s.xy * RENDERSIZE, b = s.zw * RENDERSIZE, ab = b - a;
        float len2 = dot(ab, ab);
        float t = len2 > 0.0 ? clamp(dot(px - a, ab) / len2, 0.0, 1.0) : 0.0;
        float d = length(px - (a + ab * t));
        if (d < best) {
            best = d;
            side = ab.x * (px.y - a.y) - ab.y * (px.x - a.x) < 0.0 ? -1.0 : 1.0;
            u = along == 1 ? t : mix(m.x, m.y, t);
            index = cascadeBy == 1 ? m.w : m.z;
        }
    }
    float halfWidth = thickness * 0.5;
    float cover = 1.0 - smoothstep(halfWidth - softness * halfWidth - 0.5, halfWidth + 0.5, best);
    if (reverse) u = 1.0 - u;
    float v = clamp(0.5 + side * best / max(thickness, 1e-3), 0.0, 1.0);
    float phase = TIME * rate - cascade * index; // the later ones start later
    vec4 color = gradient ? mix(lineColor, endColor, u) : lineColor;
    float lit = 1.0;
    if (animation == 1) lit = rate == 0.0 ? step(u, fill) * step(1e-4, fill) : step(u, fract(phase));
    else if (animation == 2) lit = band(u * dashes - phase, dashWidth);
    else if (animation == 3) lit = step(fract(phase), duty);
    else if (animation == 4) lit = 0.5 + 0.5 * cos(6.28318530718 * phase);
    else if (animation == 5) color = IMG_NORM_PIXEL(media, vec2(fract(u - phase), v));
    gl_FragColor = mix(backColor, color, cover * lit);
}
)ISF";

QString builtInIsfSource(const QString &path)
{
    if (isLineGeneratorPath(path)) return QString::fromUtf8(kLineShader);
    return {};
}

int LinePath::segmentCount() const
{
    const int n = int(points.size());
    return n < 2 ? 0 : closed && n > 2 ? n : n - 1;
}

static double pixelDistance(QPointF a, QPointF b, double w, double h) { return std::hypot((b.x() - a.x()) * w, (b.y() - a.y()) * h); }

double LinePath::length(double width, double height) const
{
    double total = 0;
    for (int k = 0; k < segmentCount(); ++k) total += pixelDistance(segmentStart(k), segmentEnd(k), width, height);
    return total;
}

std::vector<QPointF> LinePath::spread(int n, double width, double height) const
{
    std::vector<QPointF> out;
    if (n <= 0 || segmentCount() == 0) return out;
    const double total = length(width, height);
    if (total <= 0) return std::vector<QPointF>(size_t(n), points.front());
    int k = 0;
    double before = 0; // the length of the segments before segment k
    for (int i = 0; i < n; ++i) {
        const double at = (i + 0.5) / n * total;
        while (k < segmentCount() - 1 && before + pixelDistance(segmentStart(k), segmentEnd(k), width, height) < at) {
            before += pixelDistance(segmentStart(k), segmentEnd(k), width, height);
            ++k;
        }
        const double len = pixelDistance(segmentStart(k), segmentEnd(k), width, height);
        const double t = len > 0 ? std::clamp((at - before) / len, 0.0, 1.0) : 0.0;
        out.push_back(segmentStart(k) + (segmentEnd(k) - segmentStart(k)) * t);
    }
    return out;
}

QJsonArray linePathsToJson(const std::vector<LinePath> &lines)
{
    QJsonArray out;
    for (const LinePath &l : lines) {
        QJsonArray pts;
        for (const QPointF &p : l.points) pts.append(QJsonArray{p.x(), p.y()});
        QJsonObject o{{"points", pts}};
        if (l.closed) o["closed"] = true;
        out.append(o);
    }
    return out;
}

std::vector<LinePath> linePathsFromJson(const QJsonArray &a)
{
    std::vector<LinePath> out;
    for (const QJsonValue &v : a) {
        LinePath l;
        const QJsonObject o = v.toObject();
        for (const QJsonValue &p : o.value("points").toArray()) {
            const QJsonArray xy = p.toArray();
            if (xy.size() < 2) continue;
            const double x = xy[0].toDouble(), y = xy[1].toDouble();
            if (std::isfinite(x) && std::isfinite(y)) l.points.emplace_back(std::clamp(x, -10.0, 11.0), std::clamp(y, -10.0, 11.0));
        }
        l.closed = o.value("closed").toBool(false);
        if (l.points.size() >= 2) out.push_back(std::move(l));
    }
    return out;
}

std::vector<float> lineSegmentData(const std::vector<LinePath> &lines, double width, double height, int *count)
{
    std::vector<float> top, bottom;
    for (int li = 0; li < int(lines.size()); ++li) {
        const LinePath &l = lines[size_t(li)];
        const double total = l.length(width, height);
        double at = 0;
        for (int k = 0; k < l.segmentCount(); ++k) {
            const QPointF a = l.segmentStart(k), b = l.segmentEnd(k);
            const double len = pixelDistance(a, b, width, height);
            top.insert(top.end(), {float(a.x()), float(a.y()), float(b.x()), float(b.y())});
            const double u0 = total > 0 ? at / total : 0, u1 = total > 0 ? (at + len) / total : 1;
            bottom.insert(bottom.end(), {float(u0), float(u1), float(li), float(k)});
            at += len;
        }
    }
    if (top.empty()) { // a texture is never empty: one segment that is none
        top = {0, 0, 0, 0};
        bottom = {0, 0, -1, 0};
    }
    *count = int(top.size() / 4);
    top.insert(top.end(), bottom.begin(), bottom.end());
    return top;
}
