#pragma once
// Pixel mapping on the GPU: the colours of the composition at a list of points (Fixtures.h), read back without
// waiting. Each point becomes one texel of a small 16-bit target drawn from the composition, then read through a ring
// of pixel buffers: a sample asked for at one frame is collected at a following one, once the GPU is done with it.
// Render thread, engine context current.

#include "Gl.h"

#include <QPointF>
#include <array>
#include <functional>
#include <vector>

class PixelSampler
{
public:
    explicit PixelSampler(std::function<void()> drawQuad) : m_drawQuad(std::move(drawQuad)) {}
    bool initGl(QString *log);
    void release();

    // Colours of `picture` at `points` (normalized, origin top left); `tag` comes back with them
    void sample(GLuint picture, const std::vector<QPointF> &points, quint64 tag, bool dropIfBusy = false);
    // The oldest sample the GPU finished, if any (RGB 0..1 by point): true once per sample
    bool collect(std::vector<std::array<float, 3>> *colors, quint64 *tag);
    bool collectRgba(std::vector<std::array<float, 4>> *colors, quint64 *tag);

private:
    struct Pending {
        GLuint pbo = 0;
        GLsync fence = nullptr;
        size_t count = 0, capacity = 0; // points; bytes reserved
        int w = 0, h = 0;
        quint64 tag = 0;
        quint64 order = 0; // when asked (the oldest is collected first)
    };
    bool read(Pending &p, std::vector<std::array<float, 3>> *colors, quint64 *tag, bool wait,
              std::vector<std::array<float, 4>> *rgba = nullptr);

    std::function<void()> m_drawQuad;
    GLuint m_program = 0;
    GLint m_texLoc = -1, m_pointsLoc = -1;
    GLuint m_pointsTex = 0;
    int m_pointsW = 0, m_pointsH = 0;
    std::vector<float> m_pointsData; // what m_pointsTex holds
    GLuint m_fbo = 0, m_tex = 0;
    int m_w = 0, m_h = 0;
    std::array<Pending, 3> m_ring;
    quint64 m_order = 0;
};
