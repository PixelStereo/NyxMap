#include "PixelSampler.h"

#include <algorithm>
#include <cmath>

void PixelSampler::sample(const QByteArray &rgba, QSize size, const std::vector<QPointF> &points, quint64 tag)
{
    const int w = size.width(), h = size.height();
    if (points.empty() || w <= 0 || h <= 0 || rgba.size() < qsizetype(w) * h * 4) return;
    const auto *px = reinterpret_cast<const unsigned char *>(rgba.constData());
    Done done;
    done.tag = tag;
    done.colors.resize(points.size());
    auto texel = [&](int x, int y, int c) { return px[(size_t(std::clamp(y, 0, h - 1)) * size_t(w) + size_t(std::clamp(x, 0, w - 1))) * 4 + size_t(c)] / 255.f; };
    for (size_t k = 0; k < points.size(); ++k) {
        // The texel centres at half integers, the origin at the bottom: uv.y = 1 - y
        const double fx = points[k].x() * w - 0.5, fy = (1.0 - points[k].y()) * h - 0.5;
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
        const float tx = float(fx - x0), ty = float(fy - y0);
        for (int c = 0; c < 4; ++c) {
            const float a = texel(x0, y0, c) * (1 - tx) + texel(x0 + 1, y0, c) * tx;
            const float b = texel(x0, y0 + 1, c) * (1 - tx) + texel(x0 + 1, y0 + 1, c) * tx;
            done.colors[k][size_t(c)] = a * (1 - ty) + b * ty;
        }
    }
    m_done.push_back(std::move(done));
    while (m_done.size() > 4) m_done.pop_front(); // a reader that lags keeps the latest
}

bool PixelSampler::collect(std::vector<std::array<float, 3>> *colors, quint64 *tag)
{
    if (m_done.empty()) return false;
    const Done &d = m_done.front();
    if (colors) {
        colors->resize(d.colors.size());
        for (size_t k = 0; k < d.colors.size(); ++k) (*colors)[k] = {d.colors[k][0], d.colors[k][1], d.colors[k][2]};
    }
    if (tag) *tag = d.tag;
    m_done.pop_front();
    return true;
}

bool PixelSampler::collectRgba(std::vector<std::array<float, 4>> *colors, quint64 *tag)
{
    if (m_done.empty()) return false;
    if (colors) *colors = m_done.front().colors;
    if (tag) *tag = m_done.front().tag;
    m_done.pop_front();
    return true;
}
