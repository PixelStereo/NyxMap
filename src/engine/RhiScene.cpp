#include "RhiScene.h"

#include "RhiDevice.h"
#include "RhiResources.h"

#include <QMatrix4x4>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cstring>
#include <map>

namespace {

// The uniform blocks of the scene shaders (shaders/scene*.vert and .frag), in std140
struct Place {
    float model[16], view[16], proj[16];
    float normal[12]; // three columns of a vec4 each
    float tiling[4];  // the UV tiling, the displacement's amount and middle
    qint32 ints[4];   // hasDisp, shading, dispTopology, the shape FX's count
    qint32 fxKind[8][4];
    float fxA[8][4], fxB[8][4];
    float center[4], size[4];
    float base[4], emissive[4];
    qint32 flags[4]; // hasTex, layerTex, alphaMode, unlit
    float ink[4];
    float mat0[4];   // metal, rough, dielectric, normalScale
    float mat1[4];   // aoStrength
    qint32 has[4];   // roughness, metal, normal, occlusion maps
    qint32 has2[4];  // emission map
    float roughCh[4], metalCh[4];
};
static_assert(sizeof(Place) == 848, "the scene shaders' Place block");
constexpr quint32 kPlaceBytes = 1024;

struct SceneBlock {
    float eye[4], ambient[4], misc[4];
    qint32 counts[4];
    qint32 types[2][4];
    float color[8][4], lpos[8][4], ldir[8][4], lright[8][4], lup[8][4], lhalf[8][4], cone[8][4];
};
static_assert(sizeof(SceneBlock) == 16 * 4 + 32 + 7 * 128, "the scene shaders' Scene block");
constexpr quint32 kSceneBytes = 1024;

struct PostBlock {
    float flip[4], grade[4], more[4];
};

enum class Kind { Faces, Edges, Opaque, Blended, Lit, LitBlended };

} // namespace

struct RhiScene::Impl {
    QRhi *rhi = nullptr;
    // A target with its depth, antialiased when it has samples (resolved into `color`)
    struct Surface {
        int id = 0;
        std::unique_ptr<QRhiTexture> color;
        std::unique_ptr<QRhiRenderBuffer> ms, depth;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiRenderPassDescriptor> pass;
        QSize size;
        int samples = 1;
    };
    Surface view{1}, lit{2}, graded{3};
    std::vector<std::unique_ptr<QRhiBuffer>> ubufs;
    size_t nextUbuf = 0;
    std::unique_ptr<QRhiBuffer> sceneUbuf, postUbuf;
    std::unique_ptr<QRhiSampler> repeat; // the models' maps: repeated, mipmapped
    std::map<int, std::unique_ptr<QRhiGraphicsPipeline>> pipelines;
    std::unique_ptr<QRhiGraphicsPipeline> postPipeline;

    explicit Impl(QRhi *r) : rhi(r) {}

    QRhiBuffer *ubuf()
    {
        if (nextUbuf == ubufs.size()) {
            ubufs.emplace_back(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kPlaceBytes));
            ubufs.back()->create();
        }
        return ubufs[nextUbuf++].get();
    }

    bool ensure(Surface &s, int w, int h, int samples, bool withDepth)
    {
        if (s.target && s.size == QSize(w, h) && s.samples == samples) return true;
        s.target.reset();
        s.pass.reset();
        s.depth.reset();
        s.ms.reset();
        s.color.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(w, h), 1, QRhiTexture::RenderTarget));
        if (!s.color->create()) return false;
        QRhiColorAttachment attachment(s.color.get());
        if (samples > 1) {
            s.ms.reset(rhi->newRenderBuffer(QRhiRenderBuffer::Color, QSize(w, h), samples));
            if (!s.ms->create()) return false;
            attachment = QRhiColorAttachment(s.ms.get());
            attachment.setResolveTexture(s.color.get());
        }
        QRhiTextureRenderTargetDescription desc{attachment};
        if (withDepth) {
            s.depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, QSize(w, h), samples));
            if (!s.depth->create()) return false;
            desc.setDepthStencilBuffer(s.depth.get());
        }
        s.target.reset(rhi->newTextureRenderTarget(desc));
        s.pass.reset(s.target->newCompatibleRenderPassDescriptor());
        s.target->setRenderPassDescriptor(s.pass.get());
        if (!s.target->create()) return false;
        s.size = QSize(w, h);
        s.samples = samples;
        // Its pipelines were made for the other one
        for (auto it = pipelines.begin(); it != pipelines.end();)
            if (it->first / 100 == s.id) it = pipelines.erase(it);
            else ++it;
        if (s.id == 3) postPipeline.reset();
        return true;
    }

    QRhiGraphicsPipeline *pipeline(Surface &surface, Kind kind, QRhiShaderResourceBindings *srb)
    {
        std::unique_ptr<QRhiGraphicsPipeline> &ps = pipelines[surface.id * 100 + int(kind)];
        if (ps) return ps.get();
        ps.reset(rhi->newGraphicsPipeline());
        const auto shader = [](const char *name) { return RhiResources::shader(QString::fromLatin1(name)); };
        const char *fragment = kind == Kind::Opaque || kind == Kind::Blended ? "sceneunlit.frag"
                               : kind == Kind::Lit || kind == Kind::LitBlended ? "scenelit.frag" : "sceneplain.frag";
        ps->setShaderStages({{QRhiShaderStage::Vertex, shader("sceneview.vert")}, {QRhiShaderStage::Fragment, shader(fragment)}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{8 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                              {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
                              {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)}});
        ps->setVertexInputLayout(layout);
        ps->setTopology(kind == Kind::Edges ? QRhiGraphicsPipeline::Lines : QRhiGraphicsPipeline::Triangles);
        ps->setCullMode(QRhiGraphicsPipeline::None);
        ps->setDepthTest(true);
        const bool blended = kind == Kind::Blended || kind == Kind::LitBlended;
        ps->setDepthWrite(!blended); // blended surfaces hide nothing
        ps->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if (kind == Kind::Faces) { // behind its own edges
            ps->setDepthBias(1);
            ps->setSlopeScaledDepthBias(1.0f);
        }
        if (blended) {
            QRhiGraphicsPipeline::TargetBlend b;
            b.enable = true;
            b.srcColor = b.srcAlpha = QRhiGraphicsPipeline::One; // premultiplied
            b.dstColor = b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            ps->setTargetBlends({b});
        }
        ps->setSampleCount(surface.samples);
        ps->setShaderResourceBindings(srb);
        ps->setRenderPassDescriptor(surface.pass.get());
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
    d->sceneUbuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kSceneBytes));
    d->sceneUbuf->create();
    d->postUbuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    d->postUbuf->create();
}

RhiScene::~RhiScene() = default;

namespace {

// The camera's matrices as this backend takes them: depth from 0 to 1 where it is, and the first row of the picture at its
// bottom (RhiDevice.h)
QMatrix4x4 backendProjection(QRhi *rhi, const QMatrix4x4 &proj)
{
    QMatrix4x4 fix;
    if (rhi->isClipDepthZeroToOne()) {
        fix(2, 2) = 0.5f;
        fix(2, 3) = 0.5f;
    }
    fix(1, 1) = rhidevice::ndcFlip(rhi);
    return fix * proj;
}

struct Command {
    Kind kind;
    QRhiBuffer *ubuf;
    QRhiShaderResourceBindings *srb;
    QRhiBuffer *vertices, *indices;
    int count;
};

struct Placed {
    const Scene3D::Item *item;
    QMatrix4x4 model;
    QVector3D center;
    float radius;
};

// Whether that sphere is seen through a camera's matrix. A bent or pushed model is not where its sphere says: always
bool inView(const QMatrix4x4 &viewProj, const Placed &p)
{
    if (p.item->displace || !p.item->shape.empty()) return true;
    const QVector4D row0 = viewProj.row(0), row1 = viewProj.row(1), row2 = viewProj.row(2), row3 = viewProj.row(3);
    for (const QVector4D &plane : {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row3 + row2, row3 - row2}) {
        const float length = plane.toVector3D().length();
        if (length > 1e-6f && QVector3D::dotProduct(plane.toVector3D(), p.center) + plane.w() < -p.radius * length) return false;
    }
    return true;
}

Place place(const Scene3D::Item &item, const QMatrix4x4 &model, const Camera3D &cam, const QMatrix4x4 &proj)
{
    Place p = {};
    std::memcpy(p.model, model.constData(), sizeof p.model);
    std::memcpy(p.view, cam.view.constData(), sizeof p.view);
    std::memcpy(p.proj, proj.constData(), sizeof p.proj);
    const QMatrix3x3 n = model.normalMatrix();
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) p.normal[c * 4 + r] = n(r, c); // columns
    p.tiling[0] = item.placement.uvTiling[0];
    p.tiling[1] = item.placement.uvTiling[1];
    p.ints[1] = item.placement.shading;
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
    return p;
}

void material(Place &p, const Model3D::RhiDraw &draw, bool unlit)
{
    std::memcpy(p.base, draw.baseColor.data(), sizeof p.base);
    std::memcpy(p.emissive, draw.emissive.data(), sizeof(float) * 3);
    p.emissive[3] = draw.alphaCutoff;
    p.flags[0] = draw.baseTexture ? 1 : 0;
    p.flags[2] = draw.alphaMode;
    p.flags[3] = unlit ? 1 : 0;
    p.mat0[0] = draw.metallic;
    p.mat0[1] = draw.roughness;
    p.mat0[2] = draw.dielectric;
    p.mat0[3] = draw.normalScale;
    p.mat1[0] = draw.aoStrength;
    for (int k = 0; k < 4; ++k) p.has[k] = draw.maps[size_t(k)] ? 1 : 0;
    p.has2[0] = draw.maps[4] ? 1 : 0;
    std::memcpy(p.roughCh, draw.roughChannel.data(), sizeof p.roughCh);
    std::memcpy(p.metalCh, draw.metalChannel.data(), sizeof p.metalCh);
}

} // namespace

QRhiTexture *RhiScene::drawView(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *u, const SceneSettings &s,
                                const std::vector<Scene3D::Item> &items, int w, int h, Scene3D::ViewLook look)
{
    if (w <= 0 || h <= 0 || !d->ensure(d->view, w, h, 1, true)) return nullptr;
    QRhi *rhi = d->rhi;
    d->nextUbuf = 0;
    RhiResources &res = RhiResources::of(rhi);
    const Camera3D cam = Camera3D::of(s, float(w) / float(h));
    const QMatrix4x4 proj = backendProjection(rhi, cam.proj);
    std::vector<Command> commands;
    int triangles = 0;
    SceneBlock none = {};
    u->updateDynamicBuffer(d->sceneUbuf.get(), 0, sizeof none, &none);
    const auto fill = [&](const Place &p) {
        QRhiBuffer *b = d->ubuf();
        u->updateDynamicBuffer(b, 0, sizeof p, &p);
        return b;
    };
    const auto bind = [&](QRhiBuffer *b, QRhiTexture *base) {
        QRhiTexture *black = res.black();
        std::vector<RhiResources::Bind> bindings{
            {0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, b},
            {1, QRhiShaderResourceBinding::FragmentStage, d->sceneUbuf.get()},
            {2, QRhiShaderResourceBinding::VertexStage, nullptr, black, res.linear()},
            {3, QRhiShaderResourceBinding::FragmentStage, nullptr, base ? base : black, d->repeat.get()}};
        for (int k = 0; k < 5; ++k) bindings.push_back({4 + k, QRhiShaderResourceBinding::FragmentStage, nullptr, black, res.linear()});
        return res.bindings(bindings);
    };
    std::vector<Placed> placed;
    for (const Scene3D::Item &item : items)
        if (item.model) placed.push_back({&item, item.parent * item.model->matrix(item.placement), {}, 0});

    if (look == Scene3D::ViewLook::Unlit) {
        // The opaque surfaces, then the blended ones over them (their depth not written: what is behind them still shows)
        for (int pass = 0; pass < 2; ++pass)
            for (const Placed &pl : placed) {
                const Scene3D::Item &item = *pl.item;
                if (pass == 1 && !item.model->hasBlended()) continue;
                const std::vector<Model3D::RhiDraw> draws = item.model->rhiDraws(rhi, u, &item.placement, false, false);
                for (const Model3D::RhiDraw &draw : draws) {
                    if ((draw.alphaMode == 2) != (pass == 1)) continue;
                    Place p = place(item, pl.model, cam, proj);
                    material(p, draw, false);
                    QRhiBuffer *b = fill(p);
                    commands.push_back({pass == 1 ? Kind::Blended : Kind::Opaque, b, bind(b, draw.baseTexture), draw.vertices, draw.indices, draw.indexCount});
                    if (pass == 0) triangles += draw.indexCount / 3;
                }
            }
    } else {
        for (const Placed &pl : placed) {
            const Scene3D::Item &item = *pl.item;
            const std::vector<Model3D::RhiDraw> draws = item.model->rhiDraws(rhi, u, &item.placement, true, true) /* all of it, as the plain look draws */;
            const auto &c = item.placement.color;
            const float face[4] = {c[0] * 0.4f, c[1] * 0.4f, c[2] * 0.4f, 1};
            const float edge[4] = {c[0] * 0.4f + 0.45f, c[1] * 0.4f + 0.45f, c[2] * 0.4f + 0.45f, 1};
            for (const Model3D::RhiDraw &draw : draws) {
                Place p = place(item, pl.model, cam, proj);
                std::memcpy(p.ink, face, sizeof face);
                QRhiBuffer *b = fill(p);
                commands.push_back({Kind::Faces, b, bind(b, nullptr), draw.vertices, draw.indices, draw.indexCount});
                triangles += draw.indexCount / 3;
            }
            for (const Model3D::RhiDraw &draw : draws) {
                if (!draw.edges || !draw.edgeCount) continue;
                Place p = place(item, pl.model, cam, proj);
                std::memcpy(p.ink, edge, sizeof edge);
                QRhiBuffer *b = fill(p);
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
    cb->beginPass(d->view.target.get(), clear, {1.0f, 0}, u);
    cb->setViewport({0, 0, float(w), float(h)});
    for (const Command &c : commands) {
        QRhiGraphicsPipeline *ps = c.srb ? d->pipeline(d->view, c.kind, c.srb) : nullptr;
        if (!ps) continue;
        cb->setGraphicsPipeline(ps);
        cb->setShaderResources(c.srb);
        const QRhiCommandBuffer::VertexInput input(c.vertices, 0);
        cb->setVertexInput(0, 1, &input, c.indices, 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(quint32(c.count));
    }
    cb->endPass();
    return d->view.color.get();
}

QRhiTexture *RhiScene::draw(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *u, const SceneSettings &s, const std::vector<Scene3D::Item> &items,
                            const std::vector<Scene3D::Lamp> &lamps, int w, int h, const Scene3DQuality &quality)
{
    if (w <= 0 || h <= 0) return nullptr;
    QRhi *rhi = d->rhi;
    const int aaLevel = std::clamp(quality.antialiasing, 0, 2); // the supersampled level comes with the effects' passes
    int samples = 1;
    for (int wanted = 1 << aaLevel; wanted > 1; wanted >>= 1)
        if (rhi->supportedSampleCounts().contains(wanted)) {
            samples = wanted;
            break;
        }
    if (!d->ensure(d->lit, w, h, samples, true)) return nullptr;
    d->nextUbuf = 0;
    RhiResources &res = RhiResources::of(rhi);
    const Camera3D cam = Camera3D::of(s, float(w) / float(h));
    const QMatrix4x4 proj = backendProjection(rhi, cam.proj);

    // The scene's lights
    SceneBlock sb = {};
    sb.eye[0] = cam.eye.x(), sb.eye[1] = cam.eye.y(), sb.eye[2] = cam.eye.z();
    for (int c = 0; c < 3; ++c) sb.ambient[c] = s.ambient[size_t(c)] * s.ambientIntensity;
    sb.misc[0] = s.exposure;
    sb.misc[1] = std::max(0.0f, s.studio);
    int count = 0;
    for (const Scene3D::Lamp &lamp : lamps) {
        const LightSettings &l = lamp.light;
        const float power = l.intensity * lamp.gain;
        if (power <= 0 || count >= Scene3D::kMaxLights) continue;
        const int k = count++;
        sb.types[k / 4][k % 4] = std::clamp(l.type, 0, 3);
        const QVector3D dir = Camera3D::aim(l.yaw, l.pitch);
        const QVector3D right = QVector3D::crossProduct(dir, std::abs(dir.y()) > 0.95f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0)).normalized(),
                        upward = QVector3D::crossProduct(right, dir);
        sb.lhalf[k][0] = std::max(0.001f, l.width) * 0.5f;
        sb.lhalf[k][1] = std::max(0.001f, l.height) * 0.5f;
        for (int c = 0; c < 3; ++c) {
            sb.color[k][c] = l.color[size_t(c)] * power;
            sb.lpos[k][c] = l.position[size_t(c)];
            sb.ldir[k][c] = dir[c];
            sb.lright[k][c] = right[c];
            sb.lup[k][c] = upward[c];
        }
        const float spread = std::clamp(l.angle, 1.0f, 89.0f);
        sb.cone[k][0] = qDegreesToRadians(spread); // angles, in radians
        sb.cone[k][1] = std::min(sb.cone[k][0] - 1e-3f, qDegreesToRadians(spread * (1 - std::clamp(l.falloff, 0.0f, 1.0f))));
    }
    sb.counts[0] = count;
    u->updateDynamicBuffer(d->sceneUbuf.get(), 0, sizeof sb, &sb);

    const auto bind = [&](QRhiBuffer *b, const Model3D::RhiDraw &draw) {
        QRhiTexture *black = res.black();
        std::vector<RhiResources::Bind> bindings{
            {0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, b},
            {1, QRhiShaderResourceBinding::FragmentStage, d->sceneUbuf.get()},
            {2, QRhiShaderResourceBinding::VertexStage, nullptr, black, res.linear()},
            {3, QRhiShaderResourceBinding::FragmentStage, nullptr, draw.baseTexture ? draw.baseTexture : black, d->repeat.get()}};
        for (int k = 0; k < 5; ++k)
            bindings.push_back({4 + k, QRhiShaderResourceBinding::FragmentStage, nullptr, draw.maps[size_t(k)] ? draw.maps[size_t(k)] : black, d->repeat.get()});
        return res.bindings(bindings);
    };
    // What the camera sees, the nearest first (the blended ones over them, the farthest first)
    std::vector<Placed> placed;
    const QMatrix4x4 sees = cam.proj * cam.view;
    for (const Scene3D::Item &item : items) {
        if (!item.model) continue;
        Placed p{&item, item.parent * item.model->matrix(item.placement), {}, 0};
        p.center = p.model.map(item.model->center());
        p.radius = item.model->radius() * std::max({p.model.column(0).toVector3D().length(), p.model.column(1).toVector3D().length(), p.model.column(2).toVector3D().length()});
        if (inView(sees, p)) placed.push_back(p);
    }
    std::stable_sort(placed.begin(), placed.end(), [&](const Placed &a, const Placed &b) {
        return (a.center - cam.eye).lengthSquared() < (b.center - cam.eye).lengthSquared();
    });
    std::vector<Command> commands;
    int triangles = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (size_t order = 0; order < placed.size(); ++order) {
            const Placed &pl = placed[pass == 0 ? order : placed.size() - 1 - order];
            const Scene3D::Item &item = *pl.item;
            if (pass == 1 && !item.model->hasBlended()) continue;
            const std::vector<Model3D::RhiDraw> draws = item.model->rhiDraws(rhi, u, &item.placement, false, false);
            for (const Model3D::RhiDraw &draw : draws) {
                if ((draw.alphaMode == 2) != (pass == 1)) continue;
                Place p = place(item, pl.model, cam, proj);
                material(p, draw, item.placement.unlit);
                QRhiBuffer *b = d->ubuf();
                u->updateDynamicBuffer(b, 0, sizeof p, &p);
                commands.push_back({pass == 1 ? Kind::LitBlended : Kind::Lit, b, bind(b, draw), draw.vertices, draw.indices, draw.indexCount});
                if (pass == 0) triangles += draw.indexCount / 3;
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
    cb->beginPass(d->lit.target.get(), clear, {1.0f, 0}, u);
    cb->setViewport({0, 0, float(w), float(h)});
    for (const Command &c : commands) {
        QRhiGraphicsPipeline *ps = c.srb ? d->pipeline(d->lit, c.kind, c.srb) : nullptr;
        if (!ps) continue;
        cb->setGraphicsPipeline(ps);
        cb->setShaderResources(c.srb);
        const QRhiCommandBuffer::VertexInput input(c.vertices, 0);
        cb->setVertexInput(0, 1, &input, c.indices, 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(quint32(c.count));
    }
    cb->endPass();

    // All over the picture, as a camera does: the colours, the darker edges
    const bool graded = s.vignette > 0 || s.contrast != 1 || s.saturation != 1 || s.temperature != 0;
    if (!graded) return d->lit.color.get();
    if (!d->ensure(d->graded, w, h, 1, false)) return d->lit.color.get();
    PostBlock pb = {};
    pb.flip[0] = rhidevice::ndcFlip(rhi);
    pb.grade[0] = s.vignette;
    pb.grade[1] = s.contrast;
    pb.grade[2] = s.saturation;
    pb.grade[3] = s.temperature;
    QRhiResourceUpdateBatch *post = rhi->nextResourceUpdateBatch();
    post->updateDynamicBuffer(d->postUbuf.get(), 0, sizeof pb, &pb);
    QRhiShaderResourceBindings *srb = res.bindings({{0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, d->postUbuf.get()},
                                                    {1, QRhiShaderResourceBinding::FragmentStage, nullptr, d->lit.color.get(), res.linear()}});
    if (!d->postPipeline && srb) {
        d->postPipeline.reset(rhi->newGraphicsPipeline());
        d->postPipeline->setShaderStages({{QRhiShaderStage::Vertex, RhiResources::shader(QStringLiteral("scenepost.vert"))},
                                          {QRhiShaderStage::Fragment, RhiResources::shader(QStringLiteral("scenepost.frag"))}});
        d->postPipeline->setVertexInputLayout({});
        d->postPipeline->setShaderResourceBindings(srb);
        d->postPipeline->setRenderPassDescriptor(d->graded.pass.get());
        if (!d->postPipeline->create()) d->postPipeline.reset();
    }
    if (!d->postPipeline || !srb) return d->lit.color.get();
    cb->beginPass(d->graded.target.get(), Qt::transparent, {1.0f, 0}, post);
    cb->setGraphicsPipeline(d->postPipeline.get());
    cb->setViewport({0, 0, float(w), float(h)});
    cb->setShaderResources(srb);
    cb->draw(3);
    cb->endPass();
    return d->graded.color.get();
}
