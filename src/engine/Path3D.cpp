#include "Path3D.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr double kTau = 6.283185307179586;

// The line a Path's points follow, finely enough to travel along at an even pace
std::vector<QVector3D> pathOf(const Path3D &s, int samples)
{
    std::vector<QVector3D> p;
    const float w = std::abs(s.width), d = std::abs(s.depth);
    switch (s.kind) {
    case Path3D::Circle:
        for (int k = 0; k <= samples; ++k) {
            const double a = kTau * k / samples;
            p.emplace_back(float(std::cos(a)) * w / 2, 0.f, float(std::sin(a)) * d / 2);
        }
        break;
    case Path3D::Rectangle:
        p = {{-w / 2, 0, -d / 2}, {w / 2, 0, -d / 2}, {w / 2, 0, d / 2}, {-w / 2, 0, d / 2}, {-w / 2, 0, -d / 2}};
        break;
    case Path3D::Line:
        p = {{-w / 2, 0, 0}, {w / 2, 0, 0}};
        break;
    case Path3D::Spiral:
        for (int k = 0; k <= samples; ++k) {
            const double t = double(k) / samples, a = kTau * s.turns * t;
            p.emplace_back(float(std::cos(a) * t) * w / 2, float(t - 0.5) * s.height, float(std::sin(a) * t) * d / 2);
        }
        break;
    default: break;
    }
    return p;
}

// The point at a fraction of a polyline's length
QVector3D atLength(const std::vector<QVector3D> &line, double t)
{
    if (line.empty()) return {};
    if (line.size() == 1) return line.front();
    double total = 0;
    for (size_t k = 1; k < line.size(); ++k) total += (line[k] - line[k - 1]).length();
    if (total <= 0) return line.front();
    double left = std::clamp(t, 0.0, 1.0) * total;
    for (size_t k = 1; k < line.size(); ++k) {
        const double seg = (line[k] - line[k - 1]).length();
        if (left <= seg || k + 1 == line.size())
            return line[k - 1] + (line[k] - line[k - 1]) * float(seg > 0 ? std::min(1.0, left / seg) : 0.0);
        left -= seg;
    }
    return line.back();
}
}

QStringList Path3D::kindNames()
{
    return {QStringLiteral("circle"), QStringLiteral("rectangle"), QStringLiteral("line"), QStringLiteral("grid"),
            QStringLiteral("spiral"), QStringLiteral("sphere"), QStringLiteral("random")};
}

int Path3D::pointCount() const
{
    if (kind == Grid) return std::max(1, columns) * std::max(1, rows);
    return std::max(1, points);
}

std::vector<QVector3D> Path3D::pointsLocal() const
{
    std::vector<QVector3D> out;
    const int n = std::max(1, points);
    const float w = std::abs(width), d = std::abs(depth);
    switch (kind) {
    case Circle:
    case Rectangle:
        for (int k = 0; k < n; ++k) out.push_back(along(double(k) / n)); // evenly around, the start not repeated
        break;
    case Line:
    case Spiral:
        for (int k = 0; k < n; ++k) out.push_back(along(n > 1 ? double(k) / (n - 1) : 0.5));
        break;
    case Grid: {
        const int c = std::max(1, columns), r = std::max(1, rows);
        for (int j = 0; j < r; ++j)
            for (int i = 0; i < c; ++i)
                out.emplace_back(c > 1 ? (float(i) / (c - 1) - 0.5f) * w : 0.f, 0.f, r > 1 ? (float(j) / (r - 1) - 0.5f) * d : 0.f);
        break;
    }
    case Sphere: // a Fibonacci sphere: evenly spread, any count; radii are half the sizes (an ellipsoid when they differ)
        for (int k = 0; k < n; ++k) {
            const double y = n > 1 ? 1 - 2.0 * k / (n - 1) : 0, r = std::sqrt(std::max(0.0, 1 - y * y));
            const double a = kTau * k * 0.3819660112501051; // golden angle, in turns
            out.emplace_back(float(std::cos(a) * r) * w / 2, float(y) * std::max(w, d) / 2, float(std::sin(a) * r) * d / 2);
        }
        break;
    case Random: {
        std::mt19937 rng{static_cast<uint32_t>(seed)};
        std::uniform_real_distribution<float> u(-0.5f, 0.5f);
        for (int k = 0; k < n; ++k) {
            const float x = u(rng) * w, y = u(rng) * height, z = u(rng) * d;
            out.emplace_back(x, y, z);
        }
        break;
    }
    default: break;
    }
    return out;
}

std::vector<std::vector<QVector3D>> Path3D::linesLocal() const
{
    std::vector<std::vector<QVector3D>> out;
    if (isPath()) {
        out.push_back(pathOf(*this, 96));
    } else if (kind == Grid) {
        const auto p = pointsLocal();
        const int c = std::max(1, columns), r = std::max(1, rows);
        for (int j = 0; j < r && c > 1; ++j) out.emplace_back(p.begin() + j * c, p.begin() + (j + 1) * c);
        for (int i = 0; i < c && r > 1; ++i) {
            std::vector<QVector3D> col;
            for (int j = 0; j < r; ++j) col.push_back(p[size_t(j * c + i)]);
            out.push_back(col);
        }
    }
    return out;
}

QVector3D Path3D::along(double t) const
{
    if (!isPath()) return {};
    if (isClosed()) t -= std::floor(t);
    else {
        t = std::fmod(std::abs(t), 2.0);
        if (t > 1) t = 2 - t; // there and back
    }
    return atLength(pathOf(*this, 256), t);
}
