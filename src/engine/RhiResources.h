#pragma once
// What the parts of the QRhi renderer share, one set per QRhi (render thread): samplers, a black picture, a quad,
// the engine's baked shaders, and a plain copy between pictures of any formats and sizes.

#include <QString>
#include <map>
#include <memory>
#include <rhi/qrhi.h>
#include <vector>

class RhiTarget;

class RhiResources
{
public:
    // The set of this QRhi (made at first use). release() before the QRhi goes.
    static RhiResources &of(QRhi *rhi);
    static void release(QRhi *rhi);

    QRhi *rhi() const { return m_rhi; }
    QRhiSampler *linear() const { return m_linear.get(); }  // clamped to the edge
    QRhiSampler *nearest() const { return m_nearest.get(); }
    QRhiSampler *trilinear() const { return m_trilinear.get(); } // across the mip levels too, clamped to the edge
    QRhiTexture *black() const { return m_black.get(); }   // 1 × 1, transparent black
    QRhiBuffer *quad() const { return m_quad.get(); }      // two triangles over -1..1 (vec2 each), 6 vertices
    // The uploads the first pass of the frame must carry (the black picture, the quad), nullptr once done
    QRhiResourceUpdateBatch *takeUploads();
    float ndcFlip() const { return m_flip; }

    // A shader of the engine, baked at build time (qt_add_shaders, prefix /shaders): "blit.vert"…
    static QShader shader(const QString &name);

    // `src` drawn over the whole of `dst` (outside any pass); `uploads`, if any, go with the pass
    bool blit(QRhiCommandBuffer *cb, QRhiTexture *src, RhiTarget &dst, QRhiResourceUpdateBatch *uploads = nullptr);
    // `dst` emptied (transparent black), outside any pass
    void clear(QRhiCommandBuffer *cb, RhiTarget &dst, QRhiResourceUpdateBatch *uploads = nullptr);

    // Shader resource bindings are cached by the complete set of resources. An unused binding stays alive for a few
    // frames so commands already submitted to the GPU can finish before the binding and its resources are released.
    struct Bind {
        int binding = 0;
        QRhiShaderResourceBinding::StageFlags stages;
        QRhiBuffer *ubuf = nullptr;    // a uniform buffer, or
        QRhiTexture *tex = nullptr;    // a texture sampled through
        QRhiSampler *sampler = nullptr;
    };
    QRhiShaderResourceBindings *bindings(const std::vector<Bind> &b);
    // Called once before recording a frame; ages bindings not used in recent frames.
    void frameStarted();
    // Drop copy pipelines before retiring their source textures.
    void resetPipelines();

    ~RhiResources();

private:
    explicit RhiResources(QRhi *rhi);
    QRhi *m_rhi = nullptr;
    float m_flip = 1;
    std::unique_ptr<QRhiSampler> m_linear, m_nearest, m_trilinear;
    std::unique_ptr<QRhiTexture> m_black;
    std::unique_ptr<QRhiBuffer> m_quad, m_blitUbuf;
    std::unique_ptr<QRhiGraphicsPipeline> m_blitPs[3]; // by target format: RGBA8, RGBA16F, RGBA32F
    QRhiResourceUpdateBatch *m_uploads = nullptr;
    struct Kept {
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        quint64 used = 0;
    };
    std::map<std::vector<quint64>, Kept> m_bindings;
    quint64 m_frame = 0;
};
