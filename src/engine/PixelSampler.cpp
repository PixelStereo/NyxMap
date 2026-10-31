#include "PixelSampler.h"

#include <cmath>
#include <cstring>

#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_RG32F
#define GL_RG32F 0x8230
#endif

namespace {
constexpr int kRowPoints = 1024; // points per row of the target
}

bool PixelSampler::initGl(QString *log)
{
    // One texel per point: the composition read where the point says (its uv, origin bottom left)
    m_program = compileProgram("#version 330 core\nlayout(location=0) in vec2 a_pos;\n"
                               "void main(){ gl_Position = vec4(a_pos,0.0,1.0); }\n",
                               "#version 330 core\nuniform sampler2D u_tex; uniform sampler2D u_points; out vec4 o;\n"
                               "void main(){ vec2 uv = texelFetch(u_points, ivec2(gl_FragCoord.xy), 0).xy;\n"
                               "  o = texture(u_tex, uv); }\n",
                               log);
    if (!m_program) return false;
    auto f = gl();
    m_texLoc = f->glGetUniformLocation(m_program, "u_tex");
    m_pointsLoc = f->glGetUniformLocation(m_program, "u_points");
    return true;
}

void PixelSampler::release()
{
    auto f = gl();
    for (Pending &p : m_ring) {
        if (p.fence) f->glDeleteSync(p.fence);
        if (p.pbo) f->glDeleteBuffers(1, &p.pbo);
        p = Pending();
    }
    if (m_fbo) f->glDeleteFramebuffers(1, &m_fbo);
    if (m_tex) f->glDeleteTextures(1, &m_tex);
    if (m_pointsTex) f->glDeleteTextures(1, &m_pointsTex);
    if (m_program) f->glDeleteProgram(m_program);
    m_fbo = m_tex = m_pointsTex = m_program = 0;
    m_w = m_h = m_pointsW = m_pointsH = 0;
    m_pointsData.clear();
}

void PixelSampler::sample(GLuint picture, const std::vector<QPointF> &points, quint64 tag, bool dropIfBusy)
{
    if (!m_program || !picture || points.empty()) return;
    auto f = gl();
    const int n = int(points.size());
    const int w = std::min(n, kRowPoints), h = (n + kRowPoints - 1) / kRowPoints;

    // The points, as a float texture (uploaded again only when they change)
    std::vector<float> data(size_t(w) * h * 2, 0.0f);
    for (int k = 0; k < n; ++k) {
        data[size_t(k) * 2] = float(points[size_t(k)].x());
        data[size_t(k) * 2 + 1] = float(1.0 - points[size_t(k)].y());
    }
    if (!m_pointsTex) f->glGenTextures(1, &m_pointsTex);
    f->glBindTexture(GL_TEXTURE_2D, m_pointsTex);
    if (w != m_pointsW || h != m_pointsH) {
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, w, h, 0, GL_RG, GL_FLOAT, data.data());
        m_pointsW = w;
        m_pointsH = h;
        m_pointsData = std::move(data);
    } else if (data != m_pointsData) {
        f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RG, GL_FLOAT, data.data());
        m_pointsData = std::move(data);
    }

    // The target: 16 bits, whatever the composition's precision
    if (w != m_w || h != m_h) {
        if (!m_tex) f->glGenTextures(1, &m_tex);
        f->glBindTexture(GL_TEXTURE_2D, m_tex);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, w, h, 0, GL_RGBA, GL_UNSIGNED_SHORT, nullptr);
        if (!m_fbo) f->glGenFramebuffers(1, &m_fbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
        if (f->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            m_w = m_h = 0;
            return;
        }
        m_w = w;
        m_h = h;
    }

    // A free buffer of the ring; else the oldest, its sample let go (the GPU is three frames late)
    Pending *slot = nullptr;
    for (Pending &p : m_ring)
        if (!p.fence && (!slot || p.order < slot->order)) slot = &p;
    if (!slot) {
        if (dropIfBusy)
            return;
        for (Pending &p : m_ring)
            if (!slot || p.order < slot->order) slot = &p;
        read(*slot, nullptr, nullptr, true);
    }

    f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    f->glViewport(0, 0, w, h);
    f->glDisable(GL_BLEND);
    f->glUseProgram(m_program);
    f->glActiveTexture(GL_TEXTURE1);
    f->glBindTexture(GL_TEXTURE_2D, m_pointsTex);
    f->glUniform1i(m_pointsLoc, 1);
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, picture);
    f->glUniform1i(m_texLoc, 0);
    m_drawQuad();
    f->glActiveTexture(GL_TEXTURE1); // the float points not left where another program samples
    f->glBindTexture(GL_TEXTURE_2D, 0);
    f->glActiveTexture(GL_TEXTURE0);

    const size_t bytes = size_t(w) * h * 4 * sizeof(quint16);
    if (!slot->pbo) f->glGenBuffers(1, &slot->pbo);
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
    if (slot->capacity != bytes) {
        f->glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(bytes), nullptr, GL_STREAM_READ);
        slot->capacity = bytes;
    }
    f->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    f->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_SHORT, nullptr);
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    slot->fence = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    slot->count = size_t(n);
    slot->w = w;
    slot->h = h;
    slot->tag = tag;
    slot->order = ++m_order;
}

bool PixelSampler::read(Pending &p, std::vector<std::array<float, 3>> *colors, quint64 *tag, bool wait,
                        std::vector<std::array<float, 4>> *rgba)
{
    if (!p.fence) return false;
    auto f = gl();
    const GLenum r = f->glClientWaitSync(p.fence, GL_SYNC_FLUSH_COMMANDS_BIT, wait ? GLuint64(1000000000) : 0);
    if (r != GL_ALREADY_SIGNALED && r != GL_CONDITION_SATISFIED && !wait) return false;
    f->glDeleteSync(p.fence);
    p.fence = nullptr;
    if (!colors && !rgba)
        return false;
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, p.pbo);
    bool ok = false;
    if (const auto *px = static_cast<const quint16 *>(
            f->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(p.capacity), GL_MAP_READ_BIT))) {
        if (colors) {
            colors->resize(p.count);
            for (size_t k = 0; k < p.count; ++k)
                for (int c = 0; c < 3; ++c)
                    (*colors)[k][size_t(c)] = px[k * 4 + size_t(c)] / 65535.f;
        }
        if (rgba) {
            rgba->resize(p.count);
            for (size_t k = 0; k < p.count; ++k)
                for (int c = 0; c < 4; ++c)
                    (*rgba)[k][size_t(c)] = px[k * 4 + size_t(c)] / 65535.f;
        }
        f->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        ok = true;
    }
    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (ok && tag) *tag = p.tag;
    return ok;
}

bool PixelSampler::collect(std::vector<std::array<float, 3>> *colors, quint64 *tag)
{
    Pending *oldest = nullptr;
    for (Pending &p : m_ring)
        if (p.fence && (!oldest || p.order < oldest->order)) oldest = &p;
    return oldest && read(*oldest, colors, tag, false);
}

bool PixelSampler::collectRgba(std::vector<std::array<float, 4>> *colors, quint64 *tag)
{
    Pending *oldest = nullptr;
    for (Pending &p : m_ring)
        if (p.fence && (!oldest || p.order < oldest->order))
            oldest = &p;
    return oldest && read(*oldest, nullptr, tag, false, colors);
}
