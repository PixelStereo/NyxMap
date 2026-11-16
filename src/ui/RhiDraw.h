#pragma once
// The interface's drawing through QRhi (the widgets' own QRhi, QRhiWidget): one picture, then lines and triangles in
// normalized device coordinates (x right, y up, as OpenGL's), each batch in one color.
// Use from a QRhiWidget: initialize() with the widget's QRhi and render target, then per frame setPicture(),
// drawPicture(), drawLines(), drawTriangles(), and render().

#include <QColor>
#include <QImage>
#include <memory>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderTarget;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;

class RhiDraw
{
public:
    RhiDraw();
    ~RhiDraw();
    // The widget's QRhi and render target (again when either changes: pipelines follow the render target's passes)
    void initialize(QRhi *rhi, QRhiRenderTarget *target);
    void release();
    bool ready() const { return m_rhi != nullptr; }

    void setPicture(const QImage &image); // RGBA8888, the bottom row first (as the engine gives them); kept until the next one
    bool hasPicture() const { return m_pictureSize.isValid(); }
    QSize pictureSize() const { return m_pictureSize; }

    void begin(const QColor &background);
    // The picture on a rectangle of NDC (-1..1); over: its own alpha, blended over what is there
    void drawPicture(float left, float bottom, float right, float top, bool over = false);
    void drawLines(const std::vector<float> &ndc, const QColor &c); // pairs of (x, y) points
    void drawTriangles(const std::vector<float> &ndc, const QColor &c);
    // Records the frame's pass into the widget's command buffer
    void render(QRhiCommandBuffer *cb, QRhiRenderTarget *target);

private:
    enum class Kind { Picture, PictureOver, Lines, Triangles };
    struct Command {
        Kind kind;
        int first, count; // vertices
    };
    void add(Kind kind, const std::vector<float> &ndc, const QColor &c);
    QRhi *m_rhi = nullptr;
    QRhiRenderTarget *m_target = nullptr;
    void *m_passKey = nullptr; // the render pass descriptor the pipelines were made for
    std::unique_ptr<QRhiBuffer> m_vertices, m_uniforms;
    qsizetype m_vertexCapacity = 0;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_texture;
    QSize m_pictureSize;
    QImage m_pending; // to upload at the next render
    std::unique_ptr<QRhiShaderResourceBindings> m_pictureBindings, m_colorBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_picture, m_pictureOver, m_lines, m_triangles;
    std::vector<float> m_data; // x, y, u, v, r, g, b, a per vertex
    std::vector<Command> m_commands;
    QColor m_background;
};
