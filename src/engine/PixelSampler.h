#pragma once
// Pixel mapping: the colours of the composition at a list of points (Fixtures.h), taken from the pixels the QRhi renderer
// read back of it (a frame or a few late), bilinearly as the picture is filmed. The oldest sample is collected first.
// Render thread.

#include <QByteArray>
#include <QPointF>
#include <QSize>
#include <array>
#include <deque>
#include <vector>

class PixelSampler
{
public:
    // The colours of `rgba` (RGBA8, w × h, its first row the bottom) at `points` (normalized, origin top left); `tag`
    // comes back with them
    void sample(const QByteArray &rgba, QSize size, const std::vector<QPointF> &points, quint64 tag);
    // The oldest sample, if any (RGB 0..1 by point): true once per sample
    bool collect(std::vector<std::array<float, 3>> *colors, quint64 *tag);
    bool collectRgba(std::vector<std::array<float, 4>> *colors, quint64 *tag);
    void release() { m_done.clear(); }

private:
    struct Done {
        std::vector<std::array<float, 4>> colors;
        quint64 tag = 0;
    };
    std::deque<Done> m_done;
};
