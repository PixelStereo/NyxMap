#include "RhiScene.h"

#include "RhiDevice.h"
#include "RhiResources.h"

#include <QMatrix4x4>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cstring>
#include <map>

namespace {

// The uniform block of the scene shaders (shaders/sceneview.vert), in std140
struct Place {
    float model[16], view[16], proj[16];
    float normal[12]; // three columns of a vec4 each
    float tiling[4];  // the UV tiling, the displacement's amount and middle
    qint32 ints[4];   // hasDisp, shading, dispTopology, the shape FX's count
    qint32 fxKind[8][4];
    float fxA[8][4], fxB[8][4];
    float center[4], size[4];
    float base[4], emissive[4];
    qint32 flags[4]; // hasTex, layerTex, alphaMode
    float ink[4];
};
static_assert(sizeof(Place) == 752, "the scene shaders' uniform block");
constexpr quint32 kPlaceBytes = 768;

enum class Kind { Faces, Edges, Opaque, Blended };

} // namespace

struct RhiScene::Impl {
    QRhi *rhi = nullptr;
    std::unique_ptr<QRhiTexture> color;
    std::unique_ptr<QRhiRenderBuffer> depth;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;
    QSize size;
    std::vector<std::unique_ptr<QRhiBuffer>> ubufs;
    size_t nextUbuf = 0;
    std::unique_ptr<QRhiSampler> repeat; // the models' maps: repeated, mipmapped
    std::map<int, std::unique_ptr<QRhiGraphicsPipeline>> pipelines;

    explicit Impl(QRhi *r) : rhi(r) {}

    QRhiBuffer *ubuf()
    {
        if (nextUbuf == ubufs.size()) {
            ubufs.emplace_back(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kPlaceBytes));
            ubufs.back()->create();
        }
        return ubufs[nextUbuf++].get();
    }

    bool ensureTarget(int w, int h)
    {
        if (target && size == QSize(w, h)) return true;
        target.reset();
        pass.reset();
        depth.reset();
        color.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(w, h), 1, QRhiTexture::RenderTarget));
        if (!color->create()) return false;
        depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, QSize(w, h)));
        if (!depth->create()) return false;
        QRhiTextureRenderTargetDescription desc{QRhiColorAttachment(color.get())};
        desc.setDepthStencilBuffer(depth.get());
        target.reset(rhi->newTextureRenderTarget(desc));
        pass.reset(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());
        if (!target->create()) return false;
        size = QSize(w, h);
        pipelines.clear();
        return true;
    }

    QRhiGraphicsPipeline *pipeline(Kind kind, QRhiShaderResourceBindings *srb)
    {
        std::unique_ptr<QRhiGraphicsPipeline> &ps = pipelines[int(kind)];
        if (ps) return ps.get();
        ps.reset(rhi->newGraphicsPipeline());
        const auto shader = [](const char *name) { return RhiResources::shader(QString::fromLatin1(name)); };
        ps->setShaderStages({{QRhiShaderStage::Vertex, shader("sceneview.vert")},
                             {QRhiShaderStage::Fragment, shader(kind == Kind::Opaque || kind == Kind::Blended ? "sceneunlit.frag" : "sceneplain.frag")}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{8 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                              {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
                              {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)}});
        ps->setVertexInputLayout(layout);
        ps->setTopology(kind == Kind::Edges ? QRhiGraphicsPipeline::Lines : QRhiGraphicsPipeline::Triangles);
        ps->setCullMode(QRhiGraphicsPipeline::None);
        ps->setDepthTest(true);
        ps->setDepthWrite(kind != Kind::Blended); // blended surfaces hide nothing
        ps->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (kind == Kind::Faces) { // behind its own edges
            ps->setDepthBias(1);
            ps->setSlopeScaledDepthBias(1.0f);
        }
        if (kind == Kind::Blended) {
            QRhiGraphicsPipeline::TargetBlend b;
            b.enable = true;
            b.srcColor = b.srcAlpha = QRhiGraphicsPipeline::One; // premultiplied
            b.dstColor = b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            ps->setTargetBlends({b});
        }
        ps->setShaderResourceBindings(srb);
        ps->setRenderPassDescriptor(pass.get());
        if (!ps->create()) {
            ps.reset();
            return nullptr;
        }
        return ps.get();
    }
};

RhiScene::RhiScene(QRhi *rhi) : d(std::make_unique<Impl>(rhi))
{
    d->repeat.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Repeat, QRhiSampler::Repeat));
    d->repeat->create();
}

RhiScene::~RhiScene() = default;

QRhiTexture *RhiScene::drawView(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *u, const SceneSettings &s,
                                const std::vector<Scene3D::Item> &items, int w, int h, Scene3D::ViewLook look)
{
    if (w <= 0 || h <= 0 || !d->ensureTarget(w, h)) return nullptr;
    QRhi *rhi = d->rhi;
    d->nextUbuf = 0;
    RhiResources &res = RhiResources::of(rhi);
    // The camera's matrices as this backend takes them: depth from 0 to 1 where it is, and the first row of the picture at
    // its bottom (RhiDevice.h)
    const Camera3D cam = Camera3D::of(s, float(w) / float(h));
    QMatrix4x4 fix;
    if (rhi->isClipDepthZeroToOne()) {
        fix(2, 2) = 0.5f;
        fix(2, 3) = 0.5f;
    }
    fix(1, 1) = rhidevice::ndcFlip(rhi);
    const QMatrix4x4 proj = fix * cam.proj;

    struct Command {
        Kind kind;
        QRhiBuffer *ubuf;
        QRhiShaderResourceBindings *srb;
        QRhiBuffer *vertices, *indices;
        int count;
    };
    std::vector<Command> commands;
    int triangles = 0;
    const auto fill = [&](const Scene3D::Item &item, const QMatrix4x4 &model, const Model3D::RhiDraw *draw, const float ink[4]) {
        Place p = {};
        std::memcpy(p.model, model.constData(), sizeof p.model);
        std::memcpy(p.view, cam.view.constData(), sizeof p.view);
        std::memcpy(p.proj, proj.constData(), sizeof p.proj);
        const QMatrix3x3 n = model.normalMatrix();
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r) p.normal[c * 4 + r] = n(r, c); // columns
        p.tiling[0] = item.placement.uvTiling[0];
        p.tiling[1] = item.placement.uvTiling[1];
        const int count = int(std::min<size_t>(item.shape.size(), 8));
        p.ints[3] = count;
        for (int i = 0; i < count; ++i) {
            p.fxKind[i][0] = int(item.shape[size_t(i)].kind);
            for (int k = 0; k < 4; ++k) {
                p.fxA[i][k] = item.shape[size_t(i)].a[size_t(k)];
                p.fxB[i][k] = item.shape[size_t(i)].b[size_t(k)];
            }
        }
        const QVector3D c = item.model->center(), size = item.model->size();
        p.center[0] = c.x(), p.center[1] = c.y(), p.center[2] = c.z();
        p.size[0] = size.x(), p.size[1] = size.y(), p.size[2] = size.z();
        if (draw) {
            std::memcpy(p.base, draw->baseColor.data(), sizeof p.base);
            std::memcpy(p.emissive, draw->emissive.data(), sizeof(float) * 3);
            p.emissive[3] = draw->alphaCutoff;
            p.flags[0] = draw->baseTexture ? 1 : 0;
            p.flags[2] = draw->alphaMode;
        }
        if (ink) std::memcpy(p.ink, ink, sizeof p.ink);
        QRhiBuffer *b = d->ubuf();
        u->updateDynamicBuffer(b, 0, sizeof p, &p);
        return b;
    };
    const auto bind = [&](QRhiBuffer *b, QRhiTexture *base) {
        QRhiTexture *black = res.black();
        return res.bindings({{0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, b},
                             {1, QRhiShaderResourceBinding::VertexStage, nullptr, black, res.linear()},
                             {2, QRhiShaderResourceBinding::FragmentStage, nullptr, base ? base : black, d->repeat.get()}});
    };

    struct Placed {
        const Scene3D::Item *item;
        QMatrix4x4 model;
    };
    std::vector<Placed> placed;
    for (const Scene3D::Item &item : items)
        if (item.model) placed.push_back({&item, item.parent * item.model->matrix(item.placement)});

    if (look == Scene3D::ViewLook::Unlit) {
        // The opaque surfaces, then the blended ones over them (their depth not written: what is behind them still shows)
        for (int pass = 0; pass < 2; ++pass)
            for (const Placed &p : placed) {
                const Scene3D::Item &item = *p.item;
                // A layer's picture over the model is not taken yet: its own colours
                if (pass == 1 && !item.model->hasBlended()) continue;
                const std::vector<Model3D::RhiDraw> draws = item.model->rhiDraws(rhi, u, &item.placement, false, false);
                for (const Model3D::RhiDraw &draw : draws) {
                    if ((draw.alphaMode == 2) != (pass == 1)) continue;
                    QRhiBuffer *b = fill(item, p.model, &draw, nullptr);
                    commands.push_back({pass == 1 ? Kind::Blended : Kind::Opaque, b, bind(b, draw.baseTexture), draw.vertices, draw.indices, draw.indexCount});
                    if (pass == 0) triangles += draw.indexCount / 3;
                }
            }
    } else {
        for (const Placed &p : placed) {
            const Scene3D::Item &item = *p.item;
            const std::vector<Model3D::RhiDraw> draws = item.model->rhiDraws(rhi, u, &item.placement, true, true) /* all of it, as the plain look draws */;
            const auto &c = item.placement.color;
            const float face[4] = {c[0] * 0.4f, c[1] * 0.4f, c[2] * 0.4f, 1};
            const float edge[4] = {c[0] * 0.4f + 0.45f, c[1] * 0.4f + 0.45f, c[2] * 0.4f + 0.45f, 1};
            for (const Model3D::RhiDraw &draw : draws) {
                QRhiBuffer *b = fill(item, p.model, nullptr, face);
                commands.push_back({Kind::Faces, b, bind(b, nullptr), draw.vertices, draw.indices, draw.indexCount});
                triangles += draw.indexCount / 3;
            }
            for (const Model3D::RhiDraw &draw : draws) {
                if (!draw.edges || !draw.edgeCount) continue;
                QRhiBuffer *b = fill(item, p.model, nullptr, edge);
                commands.push_back({Kind::Edges, b, bind(b, nullptr), draw.vertices, draw.edges, draw.edgeCount});
            }
        }
    }
    m_triangles = triangles;

    const float a = s.background[3];
    const QColor clear = QColor::fromRgbF(s.background[0] * a, s.background[1] * a, s.background[2] * a, a); // premultiplied
    QRhiResourceUpdateBatch *first = res.takeUploads();
    if (first) {
        first->merge(u);
        u->release();
        u = first;
    }
    cb->beginPass(d->target.get(), clear, {1.0f, 0}, u);
    cb->setViewport({0, 0, float(w), float(h)});
    for (const Command &c : commands) {
        QRhiGraphicsPipeline *ps = c.srb ? d->pipeline(c.kind, c.srb) : nullptr;
        if (!ps) continue;
        cb->setGraphicsPipeline(ps);
        cb->setShaderResources(c.srb);
        const QRhiCommandBuffer::VertexInput input(c.vertices, 0);
        cb->setVertexInput(0, 1, &input, c.indices, 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(quint32(c.count));
    }
    cb->endPass();
    return d->color.get();
}
