#include "RhiScene.h"

#include "RhiDevice.h"
#include "RhiResources.h"
#include "RhiTarget.h"

#include <QByteArray>
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
    qint32 shadowCfg[4]; // soft edges, taps per shadow
    float screen[4];     // the picture's size, how much occlusion, how much reflection
    qint32 reflect[4];   // steps, a last picture to reflect
    float range[4];      // the near and far planes, the last picture's exposure
    float viewProj[16];
    float envInfo[4];    // intensity, rotation, blurriest level, linear
    qint32 envFlags[4];  // an environment
};
static_assert(sizeof(SceneBlock) == 80 + 48 + 64 + 32, "the scene shaders' Scene block");
constexpr quint32 kSceneBytes = 224;

struct FogBlock {
    float inv[16];
    float eye[4], color[4], a[4], b[4];
    qint32 ints[4]; // steps, lights
};

struct SkyBlock {
    float inv[16];
    float eye[4], params[4], more[4];
};
constexpr int kEnvW = 1024, kEnvH = 512;

struct AoBlock {
    float invProj[16], proj[16];
    float params[4]; // radius, amount
    qint32 ints[4];  // samples
};

struct PostBlock {
    float flip[4], grade[4], more[4];
};

enum class Kind { Faces, Edges, Opaque, Blended, Lit, LitBlended, ShadowSpot, ShadowPoint, Depth };

} // namespace

struct RhiScene::Impl {
    QRhi *rhi = nullptr;
    // A target with its depth, antialiased when it has samples (resolved into `color`)
    struct Surface {
        int id = 0;
        std::unique_ptr<QRhiTexture> color;
        std::unique_ptr<QRhiRenderBuffer> ms, depth, msBright;
        std::unique_ptr<QRhiTexture> bright; // what shines, beside the picture (the glow): a second attachment, when asked
        bool withBright = false;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiRenderPassDescriptor> pass;
        QSize size;
        int samples = 1;
    };
    Surface view{1}, lit{2}, graded{3}, seen{4}, ao{5}, aoBlurred{6}, litGlow{7}, fogA{30}, fogB{31};
    std::unique_ptr<QRhiBuffer> fogUbuf, fogBlurUbuf;
    std::vector<Surface> glowLevels; // the glow, halved again and again (ids from 20)
    std::unique_ptr<QRhiBuffer> glowUbuf[16];
    std::unique_ptr<QRhiTexture> brightPlain; // when the glow is off the post pass still wants a texture
    std::unique_ptr<QRhiBuffer> aoUbuf, blurUbuf, backgroundUbuf, skyUbuf;
    RhiTarget env;                 // the environment's copy in half floats, with its blurred levels
    QRhiTexture *envSource = nullptr;
    std::unique_ptr<QRhiSampler> envSampler;
    // A shared material's maps as textures, by their image, kept while they are used
    struct MaterialMap {
        std::unique_ptr<QRhiTexture> texture;
        quint64 used = 0;
    };
    std::map<qint64, MaterialMap> materialMaps;
    quint64 materialFrame = 0;
    RhiTarget last; // the last picture, for the reflections
    bool lastValid = false;
    std::map<int, std::unique_ptr<QRhiGraphicsPipeline>> fullscreen; // by surface and fragment shader
    std::vector<std::unique_ptr<QRhiBuffer>> ubufs;
    size_t nextUbuf = 0;
    std::unique_ptr<QRhiBuffer> sceneUbuf, postUbuf;
    std::unique_ptr<QRhiBuffer> lights; // the lights, seven vec4 each, as many as the scene has (a storage buffer)
    quint32 lightCapacity = 0;
    // The shadows: each light's view of the models in a float array (a layer each; a point light's: six), and what the
    // shader reads about them
    struct Maps {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiRenderBuffer> depth;
        struct Layer {
            std::unique_ptr<QRhiTextureRenderTarget> target;
            std::unique_ptr<QRhiRenderPassDescriptor> pass;
        };
        std::vector<Layer> layers;
        int size = 0, count = 0;
    } spotMaps, pointMaps;
    std::unique_ptr<QRhiBuffer> shadowData, pointData;
    quint32 shadowCapacity = 0, pointCapacity = 0;
    std::unique_ptr<QRhiSampler> nearest;
    int shadowPassId = 0; // the shadow passes' pipelines are keyed apart from the surfaces'

    // `count` layers of size × size, made again when it grows or its size changes
    bool ensureMaps(Maps &m, int size, int count)
    {
        if (m.texture && m.size == size && m.count >= count) return true;
        m.layers.clear();
        m.depth.reset();
        m.texture.reset(rhi->newTextureArray(QRhiTexture::R32F, count, QSize(size, size), 1, QRhiTexture::RenderTarget));
        if (!m.texture->create()) {
            m.texture.reset();
            return false;
        }
        m.depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, QSize(size, size)));
        if (!m.depth->create()) return false;
        m.size = size;
        m.count = count;
        m.layers.resize(size_t(count));
        for (int k = 0; k < count; ++k) {
            QRhiColorAttachment attachment(m.texture.get());
            attachment.setLayer(k);
            QRhiTextureRenderTargetDescription desc{attachment};
            desc.setDepthStencilBuffer(m.depth.get());
            m.layers[size_t(k)].target.reset(rhi->newTextureRenderTarget(desc));
            m.layers[size_t(k)].pass.reset(m.layers[size_t(k)].target->newCompatibleRenderPassDescriptor());
            m.layers[size_t(k)].target->setRenderPassDescriptor(m.layers[size_t(k)].pass.get());
            if (!m.layers[size_t(k)].target->create()) return false;
        }
        for (auto it = pipelines.begin(); it != pipelines.end();)
            if (it->first / 100 == 9) it = pipelines.erase(it);
            else ++it;
        return true;
    }
    std::unique_ptr<QRhiSampler> repeat; // the models' maps: repeated, mipmapped
    std::map<int, std::unique_ptr<QRhiGraphicsPipeline>> pipelines;
    std::unique_ptr<QRhiGraphicsPipeline> postPipeline;

    explicit Impl(QRhi *r) : rhi(r) {}
    QByteArray frameKey;          // what the last picture was made of
    int stillFrames = 0;          // frames in a row with that same key
    QRhiTexture *keptPicture = nullptr; // the last picture

    QRhiBuffer *ubuf()
    {
        if (nextUbuf == ubufs.size()) {
            ubufs.emplace_back(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kPlaceBytes));
            ubufs.back()->create();
        }
        return ubufs[nextUbuf++].get();
    }

    bool ensure(Surface &s, int w, int h, int samples, bool withDepth, QRhiTexture::Format format = QRhiTexture::RGBA8, bool withBright = false,
                bool preserve = false)
    {
        if (s.target && s.size == QSize(w, h) && s.samples == samples) return true;
        s.bright.reset();
        s.msBright.reset();
        s.withBright = withBright;
        s.target.reset();
        s.pass.reset();
        s.depth.reset();
        s.ms.reset();
        s.color.reset(rhi->newTexture(format, QSize(w, h), 1, QRhiTexture::RenderTarget));
        if (!s.color->create()) return false;
        QRhiColorAttachment attachment(s.color.get());
        if (samples > 1) {
            s.ms.reset(rhi->newRenderBuffer(QRhiRenderBuffer::Color, QSize(w, h), samples));
            if (!s.ms->create()) return false;
            attachment = QRhiColorAttachment(s.ms.get());
            attachment.setResolveTexture(s.color.get());
        }
        QRhiTextureRenderTargetDescription desc{attachment};
        if (withBright) { // a second attachment, in float (what shines more than a screen can show)
            s.bright.reset(rhi->newTexture(QRhiTexture::RGBA16F, QSize(w, h), 1, QRhiTexture::RenderTarget));
            if (!s.bright->create()) return false;
            QRhiColorAttachment second(s.bright.get());
            if (samples > 1) {
                s.msBright.reset(rhi->newRenderBuffer(QRhiRenderBuffer::Color, QSize(w, h), samples, {}, QRhiTexture::RGBA16F));
                if (!s.msBright->create()) return false;
                second = QRhiColorAttachment(s.msBright.get());
                second.setResolveTexture(s.bright.get());
            }
            desc.setColorAttachments({attachment, second});
        }
        if (withDepth) {
            s.depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, QSize(w, h), samples));
            if (!s.depth->create()) return false;
            desc.setDepthStencilBuffer(s.depth.get());
        }
        s.target.reset(rhi->newTextureRenderTarget(desc, preserve ? QRhiTextureRenderTarget::PreserveColorContents : QRhiTextureRenderTarget::Flags()));
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

    // The pipelines that draw into a light's map (its render pass descriptor: any layer's)
    QRhiGraphicsPipeline *shadowPipeline(Maps &maps, Kind kind, QRhiShaderResourceBindings *srb)
    {
        std::unique_ptr<QRhiGraphicsPipeline> &ps = pipelines[900 + int(kind)];
        if (ps) return ps.get();
        ps.reset(rhi->newGraphicsPipeline());
        const auto shader = [](const char *name) { return RhiResources::shader(QString::fromLatin1(name)); };
        ps->setShaderStages({{QRhiShaderStage::Vertex, shader("sceneview.vert")},
                             {QRhiShaderStage::Fragment, shader(kind == Kind::ShadowPoint ? "scenepoint.frag" : "sceneshadow.frag")}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{8 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                              {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
                              {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)}});
        ps->setVertexInputLayout(layout);
        ps->setCullMode(QRhiGraphicsPipeline::None);
        ps->setDepthTest(true);
        ps->setDepthWrite(true);
        ps->setDepthOp(QRhiGraphicsPipeline::Less);
        ps->setShaderResourceBindings(srb);
        ps->setRenderPassDescriptor(maps.layers.front().pass.get());
        if (!ps->create()) {
            ps.reset();
            return nullptr;
        }
        return ps.get();
    }

    QRhiGraphicsPipeline *pipeline(Surface &surface, Kind kind, QRhiShaderResourceBindings *srb)
    {
        std::unique_ptr<QRhiGraphicsPipeline> &ps = pipelines[surface.id * 100 + int(kind)];
        if (ps) return ps.get();
        ps.reset(rhi->newGraphicsPipeline());
        const auto shader = [](const char *name) { return RhiResources::shader(QString::fromLatin1(name)); };
        const char *fragment = kind == Kind::Opaque || kind == Kind::Blended ? "sceneunlit.frag"
                               : kind == Kind::Lit || kind == Kind::LitBlended ? (surface.withBright ? "scenelit.frag" : "scenelit_noglow.frag")
                               : kind == Kind::Depth ? "scenedepth.frag" : "sceneplain.frag";
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
            if (surface.withBright) ps->setTargetBlends({b, b});
            else ps->setTargetBlends({b});
        } else if (surface.withBright) {
            ps->setTargetBlends({QRhiGraphicsPipeline::TargetBlend(), QRhiGraphicsPipeline::TargetBlend()});
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
    d->envSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Repeat, QRhiSampler::ClampToEdge)); // around: no seam
    d->envSampler->create();
    d->nearest.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    d->nearest->create();
}

RhiScene::~RhiScene() = default;

QRhi *RhiScene::rhiOwner() const { return d->rhi; }

// The items with their shared materials' maps as textures of this QRhi (a height map pushes the points as a displacement)
std::vector<Scene3D::Item> RhiScene::withMaterials(QRhiResourceUpdateBatch *u, const std::vector<Scene3D::Item> &items)
{
    std::vector<Scene3D::Item> out = items;
    ++d->materialFrame;
    for (Scene3D::Item &item : out) {
        if (!item.material || !item.placement.sharedMaterial) continue;
        const Material3D &material = *item.material;
        for (int slot = 0; slot < MaterialMapCount; ++slot) {
            const QImage &image = material.images[size_t(slot)];
            if (image.isNull() || (slot == HeightMap && material.height == 0)) continue;
            Impl::MaterialMap &entry = d->materialMaps[image.cacheKey()];
            if (!entry.texture) {
                const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
                entry.texture.reset(d->rhi->newTexture(QRhiTexture::RGBA8, rgba.size(), 1, QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
                if (entry.texture->create()) {
                    u->uploadTexture(entry.texture.get(), rgba);
                    u->generateMips(entry.texture.get());
                } else {
                    entry.texture.reset();
                }
            }
            entry.used = d->materialFrame;
            item.placement.materialRhi[size_t(slot)] = entry.texture.get();
        }
        if (!item.displaceRhi && item.placement.materialRhi[HeightMap]) {
            item.displaceRhi = item.placement.materialRhi[HeightMap];
            item.placement.materialHeight = true;
            item.displaceAmount = material.height;
            item.displaceMid = 0;
        }
    }
    for (auto it = d->materialMaps.begin(); it != d->materialMaps.end();) {
        if (d->materialFrame - it->second.used > 240) it = d->materialMaps.erase(it);
        else ++it;
    }
    return out;
}

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
    if (p.item->displaceRhi || !p.item->shape.empty()) return true;
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
    p.tiling[2] = item.displaceAmount;
    p.tiling[3] = item.displaceMid;
    p.ints[0] = item.displaceRhi ? (item.placement.materialHeight ? 2 : 1) : 0; // a layer's picture pushing the points, or a material's height map
    p.ints[1] = item.placement.shading;
    p.ints[2] = item.model->displacementUvTopology();
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

void material(Place &p, const Model3D::RhiDraw &draw, bool unlit, bool picture)
{
    std::memcpy(p.base, draw.baseColor.data(), sizeof p.base);
    std::memcpy(p.emissive, draw.emissive.data(), sizeof(float) * 3);
    p.emissive[3] = draw.alphaCutoff;
    p.flags[0] = draw.baseTexture ? 1 : 0;
    p.flags[1] = picture ? 1 : 0; // a layer's picture: its first row is the bottom
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
                                const std::vector<Scene3D::Item> &sourceItems, int w, int h, Scene3D::ViewLook look)
{
    if (w <= 0 || h <= 0 || !d->ensure(d->view, w, h, 1, true)) return nullptr;
    const std::vector<Scene3D::Item> items = withMaterials(u, sourceItems);
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
    const auto bind = [&](QRhiBuffer *b, QRhiTexture *base, QRhiTexture *displace) {
        QRhiTexture *black = res.black();
        std::vector<RhiResources::Bind> bindings{
            {0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, b},
            {1, QRhiShaderResourceBinding::FragmentStage, d->sceneUbuf.get()},
            {2, QRhiShaderResourceBinding::VertexStage, nullptr, displace ? displace : black, res.linear()},
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
                const bool pictured = item.pictureRhi != nullptr;
                if (pass == 1 && (pictured || !item.model->hasBlended())) continue;
                std::vector<Model3D::RhiDraw> draws = item.model->rhiDraws(rhi, u, &item.placement, pictured, false);
                for (Model3D::RhiDraw &draw : draws) {
                    if (draw.blended != (pass == 1)) continue;
                    if (pictured) draw.baseTexture = item.pictureRhi;
                    Place p = place(item, pl.model, cam, proj);
                    material(p, draw, false, pictured);
                    QRhiBuffer *b = fill(p);
                    commands.push_back({pass == 1 ? Kind::Blended : Kind::Opaque, b, bind(b, draw.baseTexture, item.displaceRhi), draw.vertices, draw.indices, draw.indexCount});
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
                commands.push_back({Kind::Faces, b, bind(b, nullptr, item.displaceRhi), draw.vertices, draw.indices, draw.indexCount});
                triangles += draw.indexCount / 3;
            }
            for (const Model3D::RhiDraw &draw : draws) {
                if (!draw.edges || !draw.edgeCount) continue;
                Place p = place(item, pl.model, cam, proj);
                std::memcpy(p.ink, edge, sizeof edge);
                QRhiBuffer *b = fill(p);
                commands.push_back({Kind::Edges, b, bind(b, nullptr, item.displaceRhi), draw.vertices, draw.edges, draw.edgeCount});
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
        cb->setViewport({0, 0, float(w), float(h)}); // after the pipeline: Vulkan then sets the scissor to it
        cb->setShaderResources(c.srb);
        const QRhiCommandBuffer::VertexInput input(c.vertices, 0);
        cb->setVertexInput(0, 1, &input, c.indices, 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(quint32(c.count));
    }
    cb->endPass();
    return d->view.color.get();
}

QRhiTexture *RhiScene::draw(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *u, const SceneSettings &s, const std::vector<Scene3D::Item> &sourceItems,
                            const std::vector<Scene3D::Lamp> &lamps, int w, int h, const Scene3DQuality &quality, QRhiTexture *environment,
                            bool environmentLinear)
{
    if (w <= 0 || h <= 0) return nullptr;
    // Nothing changed for a few frames (a reflection reads the frame before, so it takes a few to settle): the picture is kept
    QByteArray key;
    bool could = !environment || environmentLinear; // an HDR file's picture does not change
    {
        const std::vector<Scene3D::Item> items = withMaterials(u, sourceItems);
        const auto add = [&key](const void *data, size_t size) { key.append(static_cast<const char *>(data), qsizetype(size)); };
        add(&environment, sizeof environment);
        add(&w, sizeof w);
        add(&h, sizeof h);
        for (float v : {s.distance, s.yaw, s.pitch, s.fov, s.ambientIntensity, s.exposure, s.envIntensity, s.envRotation, s.envBlur, s.studio,
                        s.ao, s.aoRadius, s.reflections, s.glow, s.glowThreshold, s.glowSize, s.vignette, s.contrast, s.saturation,
                        s.temperature, s.fogDensity, s.fogBase, s.fogFalloff, s.fogReach})
            add(&v, sizeof v);
        add(s.fogColor.data(), sizeof(float) * 3);
        add(s.target.data(), sizeof(float) * 3);
        add(s.ambient.data(), sizeof(float) * 3);
        add(s.background.data(), sizeof(float) * 4);
        add(&s.envShow, sizeof(bool));
        for (int v : {quality.antialiasing, quality.shadows, quality.contact, quality.reflections, quality.glow, quality.atmosphere}) add(&v, sizeof v);
        for (const Scene3D::Item &item : items) {
            if (!item.model) continue;
            could = could && !item.pictureRhi && (!item.displaceRhi || item.placement.materialHeight);
            const Model3D *which = item.model.get();
            add(&which, sizeof which);
            const QMatrix4x4 model = item.parent * item.model->matrix(item.placement);
            add(model.constData(), sizeof(float) * 16);
            const ModelPlacement &p = item.placement;
            add(p.color.data(), sizeof(float) * 3);
            add(p.uvTiling.data(), sizeof(float) * 2);
            add(p.emission.data(), sizeof(float) * 3);
            add(&item.displaceAmount, sizeof(float));
            add(&item.displaceMid, sizeof(float));
            for (float v : {p.metal, p.rough, p.normalStrength, p.occlusionStrength, p.ior, p.specular}) add(&v, sizeof v);
            for (const Scene3D::Item::ShapeOp &op : item.shape) { // its shape FX, in order
                add(&op.kind, sizeof op.kind);
                add(op.a.data(), sizeof(float) * 4);
                add(op.b.data(), sizeof(float) * 4);
            }
            for (int v : {p.detail, p.shading, int(p.unlit), int(p.sharedMaterial)}) add(&v, sizeof v);
            for (QRhiTexture *t : p.materialRhi) add(&t, sizeof t); // its shared material's maps (another picture: another texture)
            if (item.material) add(&item.material->height, sizeof item.material->height);
        }
        for (const Scene3D::Lamp &lamp : lamps) {
            const LightSettings &l = lamp.light;
            add(&l.type, sizeof l.type);
            add(l.color.data(), sizeof(float) * 3);
            add(l.position.data(), sizeof(float) * 3);
            for (float v : {l.intensity, l.yaw, l.pitch, l.angle, l.falloff, l.softness, l.width, l.height, l.rays, lamp.gain}) add(&v, sizeof v);
            add(&l.shadow, sizeof(bool));
        }
    }
    if (could && d->keptPicture && key == d->frameKey) {
        if (++d->stillFrames >= 3) {
            m_triangles = 0;
            cb->resourceUpdate(u); // what was given to upload still goes (and the batch is not left to the pool)
            return d->keptPicture;
        }
    } else {
        d->stillFrames = 0;
    }
    d->frameKey = key;
    d->keptPicture = drawNow(cb, u, s, sourceItems, lamps, w, h, quality, environment, environmentLinear);
    return d->keptPicture;
}

QRhiTexture *RhiScene::drawNow(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *u, const SceneSettings &s, const std::vector<Scene3D::Item> &sourceItems,
                               const std::vector<Scene3D::Lamp> &lamps, int w, int h, const Scene3DQuality &quality, QRhiTexture *environment,
                               bool environmentLinear)
{
    const std::vector<Scene3D::Item> items = withMaterials(u, sourceItems);
    QRhi *rhi = d->rhi;
    const bool lite = Scene3D::lite(rhi->driverInfo().deviceName); // on the processor: no antialiasing, shadows, contact shadows, reflections, glow or fog
    const int aaLevel = lite ? 0 : std::clamp(quality.antialiasing, 0, 2); // the supersampled level comes with the effects' passes
    int samples = 1;
    for (int wanted = 1 << aaLevel; wanted > 1; wanted >>= 1)
        if (rhi->supportedSampleCounts().contains(wanted)) {
            samples = wanted;
            break;
        }
    const bool wantsGlow = !lite && quality.glow > 0 && s.glow > 0;
    Impl::Surface &main = wantsGlow ? d->litGlow : d->lit;
    if (!d->ensure(main, w, h, samples, true, QRhiTexture::RGBA8, wantsGlow)) return nullptr;
    d->nextUbuf = 0;
    RhiResources &res = RhiResources::of(rhi);
    const Camera3D cam = Camera3D::of(s, float(w) / float(h));
    const QMatrix4x4 proj = backendProjection(rhi, cam.proj);

    // All the models, placed
    std::vector<Placed> all;
    for (const Scene3D::Item &item : items) {
        if (!item.model) continue;
        Placed p{&item, item.parent * item.model->matrix(item.placement), {}, 0};
        p.center = p.model.map(item.model->center());
        p.radius = item.model->radius() * std::max({p.model.column(0).toVector3D().length(), p.model.column(1).toVector3D().length(), p.model.column(2).toVector3D().length()});
        all.push_back(p);
    }

    // The shadows' share of the work, as the quality asks
    const int shadowLevel = lite ? 0 : std::clamp(quality.shadows, 0, 3);
    const int shadowSize = shadowLevel ? 512 << shadowLevel : 0, pointSize = shadowSize / 2;
    // The models that cast: all but screens (unlit) and what is far larger than the others (a floor, a wall only receive)
    QVector3D center;
    float reach = 0;
    std::vector<float> sizes;
    for (const Placed &p : all)
        if (!p.item->placement.unlit) sizes.push_back(p.radius);
    std::sort(sizes.begin(), sizes.end());
    const float largest = sizes.empty() ? 0.0f : sizes[sizes.size() / 2] * 4;
    const auto casts = [&](const Placed &p) { return !p.item->placement.unlit && p.radius <= largest; };
    {
        float weight = 0;
        for (const Placed &p : all)
            if (casts(p)) {
                center += p.center;
                weight += 1;
            }
        if (weight > 0) center /= weight;
        for (const Placed &p : all)
            if (casts(p)) reach = std::max(reach, (p.center - center).length() + p.radius);
    }

    // The scene's lights and their shadows: no limit to either but the memory
    SceneBlock sb = {};
    sb.eye[0] = cam.eye.x(), sb.eye[1] = cam.eye.y(), sb.eye[2] = cam.eye.z();
    for (int c = 0; c < 3; ++c) sb.ambient[c] = s.ambient[size_t(c)] * s.ambientIntensity;
    sb.misc[0] = s.exposure;
    sb.misc[1] = std::max(0.0f, s.studio);
    sb.shadowCfg[0] = shadowLevel >= 2 ? 1 : 0;
    sb.shadowCfg[1] = shadowLevel == 1 ? 4 : shadowLevel == 3 ? 24 : 12;
    const int contactLevel = lite ? 0 : std::clamp(quality.contact, 0, 3), reflectLevel = lite ? 0 : std::clamp(quality.reflections, 0, 3);
    const bool wantsAo = contactLevel > 0 && s.ao > 0;
    const float reflections = reflectLevel ? s.reflections : 0.0f;
    const bool wantsFog = !lite && quality.atmosphere > 0 && s.fogDensity > 0;
    const bool wantsSeen = wantsAo || reflections > 0 || wantsFog;
    sb.screen[0] = float(w), sb.screen[1] = float(h);
    sb.screen[2] = wantsAo ? s.ao : 0.0f;
    sb.screen[3] = reflections;
    sb.reflect[0] = reflectLevel == 1 ? 20 : reflectLevel == 3 ? 80 : 40;
    sb.reflect[1] = d->lastValid && reflections > 0 ? 1 : 0;
    sb.range[0] = cam.nearPlane, sb.range[1] = cam.farPlane, sb.range[2] = s.exposure, sb.range[3] = s.glowThreshold;
    {
        const QMatrix4x4 viewProj = cam.proj * cam.view; // the shader works in OpenGL's clip space (depth -1..1)
        std::memcpy(sb.viewProj, viewProj.constData(), sizeof sb.viewProj);
    }
    // The environment: a copy in half floats, then its levels (the rougher a surface, the blurrier its reflection). An HDR
    // file's picture does not change: copied once
    bool hasEnv = false;
    if (environment) {
        d->env.ensureMipped(rhi, QSize(kEnvW, kEnvH), QRhiTexture::RGBA16F);
        hasEnv = d->env.readyMipped(QSize(kEnvW, kEnvH), QRhiTexture::RGBA16F);
        if (hasEnv && !(environmentLinear && environment == d->envSource)) {
            hasEnv = res.blit(cb, environment, d->env);
            if (hasEnv) {
                QRhiResourceUpdateBatch *levels = rhi->nextResourceUpdateBatch();
                levels->generateMips(d->env.texture());
                cb->resourceUpdate(levels);
                d->envSource = environmentLinear ? environment : nullptr;
            }
        }
    }
    if (hasEnv && s.envShow) {
        if (!d->skyUbuf) {
            d->skyUbuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(SkyBlock)));
            d->skyUbuf->create();
        }
        SkyBlock sk = {};
        const QMatrix4x4 inv = (cam.proj * cam.view).inverted();
        std::memcpy(sk.inv, inv.constData(), sizeof sk.inv);
        sk.eye[0] = cam.eye.x(), sk.eye[1] = cam.eye.y(), sk.eye[2] = cam.eye.z();
        sk.params[0] = s.envIntensity;
        sk.params[1] = qDegreesToRadians(s.envRotation);
        sk.params[2] = std::clamp(s.envBlur, 0.0f, 1.0f) * (std::log2(float(kEnvW)) - 2);
        sk.params[3] = s.exposure;
        sk.more[0] = environmentLinear ? 1.0f : 0.0f;
        sk.more[1] = s.glowThreshold;
        u->updateDynamicBuffer(d->skyUbuf.get(), 0, sizeof sk, &sk);
    }
    sb.envFlags[0] = hasEnv ? 1 : 0;
    sb.envInfo[0] = s.envIntensity;
    sb.envInfo[1] = qDegreesToRadians(s.envRotation);
    sb.envInfo[2] = std::log2(float(kEnvW));
    sb.envInfo[3] = environmentLinear ? 1.0f : 0.0f;
    struct SpotShadow {
        QMatrix4x4 matrix; // the light's view and projection (clip space -1..1 in depth)
        QVector3D planes;
        float radius, bias;
    };
    struct PointShadow {
        QVector3D at;
        float far, soft;
    };
    std::vector<SpotShadow> spots;
    std::vector<PointShadow> points;
    std::vector<float> light; // seven vec4 each
    for (const Scene3D::Lamp &lamp : lamps) {
        const LightSettings &l = lamp.light;
        const float power = l.intensity * lamp.gain;
        if (power <= 0) continue;
        const QVector3D at(l.position[0], l.position[1], l.position[2]), dir = Camera3D::aim(l.yaw, l.pitch);
        const QVector3D right = QVector3D::crossProduct(dir, std::abs(dir.y()) > 0.95f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0)).normalized(),
                        upward = QVector3D::crossProduct(right, dir);
        const float spread = std::clamp(l.angle, 1.0f, 89.0f), cone = qDegreesToRadians(spread); // angles, in radians
        const float coneIn = std::min(cone - 1e-3f, qDegreesToRadians(spread * (1 - std::clamp(l.falloff, 0.0f, 1.0f))));
        float spotSlot = -1, pointSlot = -1;
        if (shadowLevel && l.shadow && reach > 0) {
            if (l.type == 1) { // a point: the distances all around it
                pointSlot = float(points.size());
                points.push_back({at, std::max((center - at).length() + reach * 3, 1.0f), std::max(0.0f, l.softness)});
            } else {
                QMatrix4x4 lightView, lightProj;
                float across; // the width its map covers where the models are
                QVector3D planes;
                if (l.type == 0) {
                    const QVector3D up = std::abs(dir.y()) > 0.95f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
                    lightView.lookAt(center - dir * reach * 2.5f, center, up);
                    lightProj.ortho(-reach, reach, -reach, reach, reach * 0.1f, reach * 5);
                    across = reach * 2;
                    planes = QVector3D(reach * 0.1f, reach * 5, 1);
                } else {
                    const float away = (center - at).length();
                    const QVector3D aim = l.type == 3 && away > reach ? (center - at).normalized() : dir;
                    // An area shines all around what it faces: its map as tight around the models as can be
                    const float half = l.type == 3 ? std::clamp(qRadiansToDegrees(std::atan2(reach * 1.2f, std::max(away, 0.1f))), 5.0f, 80.0f)
                                                   : std::clamp(l.angle + 2.0f, 2.0f, 80.0f);
                    const QVector3D up = std::abs(aim.y()) > 0.95f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
                    lightView.lookAt(at, at + aim, up);
                    const float farPlane = std::max(away + reach * 1.5f, 1.0f);
                    lightProj.perspective(half * 2, 1, std::max(0.05f, farPlane * 0.002f), farPlane);
                    planes = QVector3D(std::max(0.05f, farPlane * 0.002f), farPlane, 0);
                    across = 2 * std::max(away, 0.1f) * std::tan(qDegreesToRadians(half));
                }
                SpotShadow sh;
                sh.matrix = lightProj * lightView;
                sh.planes = planes;
                const float texel = across / float(shadowSize);
                sh.radius = std::max(0.0f, l.softness) * 2.0f / float(shadowSize);
                if (l.type == 3) sh.radius += std::max(l.width, l.height) / across * 0.07f; // as wide as it is
                sh.bias = texel * 1.5f + across * 0.0008f * std::max(0.0f, l.softness);
                if (l.type == 3) sh.bias += std::max(l.width, l.height) * 0.08f; // its wide edge reaches round a curved surface
                spotSlot = float(spots.size());
                spots.push_back(sh);
            }
        }
        const float entry[7][4] = {
            {l.color[0] * power, l.color[1] * power, l.color[2] * power, float(std::clamp(l.type, 0, 3))},
            {l.position[0], l.position[1], l.position[2], pointSlot},
            {dir.x(), dir.y(), dir.z(), spotSlot},
            {right.x(), right.y(), right.z(), 0},
            {upward.x(), upward.y(), upward.z(), 0},
            {std::max(0.001f, l.width) * 0.5f, std::max(0.001f, l.height) * 0.5f, std::max(0.0f, l.rays), 0}, // z: how much it shows in the fog
            {cone, coneIn, 0, 0}};
        light.insert(light.end(), &entry[0][0], &entry[0][0] + 28);
        ++sb.counts[0];
    }
    if (light.empty()) light.assign(28, 0.0f); // a buffer is never empty
    const auto upload = [&](std::unique_ptr<QRhiBuffer> &buffer, quint32 &capacity, const std::vector<float> &data) {
        const quint32 bytes = quint32(std::max<size_t>(data.size(), 4) * sizeof(float));
        if (!buffer || capacity < bytes) {
            capacity = std::max<quint32>(bytes, 1024) * 2;
            buffer.reset(rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, capacity));
            buffer->create();
        }
        std::vector<float> padded = data;
        padded.resize(std::max<size_t>(data.size(), 4), 0.0f);
        u->uploadStaticBuffer(buffer.get(), 0, bytes, padded.data());
    };
    upload(d->lights, d->lightCapacity, light);
    std::vector<float> spotData, pointData;
    for (const SpotShadow &sh : spots) {
        spotData.insert(spotData.end(), sh.matrix.constData(), sh.matrix.constData() + 16);
        spotData.insert(spotData.end(), {sh.planes.x(), sh.planes.y(), sh.planes.z(), 0.f, sh.radius, sh.bias, 0.f, 0.f});
    }
    for (const PointShadow &sh : points) pointData.insert(pointData.end(), {sh.at.x(), sh.at.y(), sh.at.z(), sh.far, sh.soft, 0.f, 0.f, 0.f});
    upload(d->shadowData, d->shadowCapacity, spotData);
    upload(d->pointData, d->pointCapacity, pointData);
    u->updateDynamicBuffer(d->sceneUbuf.get(), 0, sizeof sb, &sb);
    // The maps: one layer for each spot or directional light, six for each point light (at least one, never empty)
    const int mapSize = std::max(shadowSize, 4);
    if (!d->ensureMaps(d->spotMaps, mapSize, std::max<int>(1, int(spots.size()))) ||
        !d->ensureMaps(d->pointMaps, std::max(pointSize, 4), std::max<int>(6, int(points.size()) * 6)))
        return nullptr;
    if (wantsSeen) {
        if (!d->ensure(d->seen, w, h, 1, true, QRhiTexture::R32F)) return nullptr;
        if (wantsAo && (!d->ensure(d->ao, std::max(1, w / 2), std::max(1, h / 2), 1, false) ||
                        !d->ensure(d->aoBlurred, std::max(1, w / 2), std::max(1, h / 2), 1, false)))
            return nullptr;
        if (reflections > 0) d->last.ensure(rhi, QSize(w, h));
        if (wantsFog && (!d->ensure(d->fogA, std::max(1, w / 2), std::max(1, h / 2), 1, false, QRhiTexture::RGBA16F) ||
                         !d->ensure(d->fogB, std::max(1, w / 2), std::max(1, h / 2), 1, false, QRhiTexture::RGBA16F)))
            return nullptr;
    }
    // The flip of the fullscreen passes' triangle
    PostBlock pb = {};
    pb.flip[0] = rhidevice::ndcFlip(rhi);
    pb.grade[0] = s.vignette;
    pb.grade[1] = s.contrast;
    pb.grade[2] = s.saturation;
    pb.grade[3] = s.temperature;
    u->updateDynamicBuffer(d->postUbuf.get(), 0, sizeof pb, &pb);

    // What the camera sees, the nearest first (the blended ones over them, the farthest first)
    std::vector<Placed> placed;
    const QMatrix4x4 sees = cam.proj * cam.view;
    for (const Placed &p : all)
        if (inView(sees, p)) placed.push_back(p);
    std::stable_sort(placed.begin(), placed.end(), [&](const Placed &a, const Placed &b) {
        return (a.center - cam.eye).lengthSquared() < (b.center - cam.eye).lengthSquared();
    });

    // The lights' views of the models that cast
    struct ShadowDraw {
        Kind kind;
        Impl::Maps *maps;
        int layer;
        std::vector<Command> commands;
        QSize size;
    };
    std::vector<ShadowDraw> shadowDraws;
    const auto shadowBind = [&](QRhiBuffer *b, QRhiTexture *displace) {
        return res.bindings({{0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, b},
                             {2, QRhiShaderResourceBinding::VertexStage, nullptr, displace ? displace : res.black(), res.linear()}});
    };
    const auto casters = [&](ShadowDraw &draw, const QMatrix4x4 &lightSees, const QMatrix4x4 &fixed, const QVector4D &ink) {
        for (const Placed &p : all) {
            if (!casts(p) || !inView(lightSees, p)) continue; // a screen gives light, a floor or a wall only receives
            const std::vector<Model3D::RhiDraw> draws = p.item->model->rhiDraws(rhi, u, &p.item->placement, p.item->pictureRhi != nullptr, false);
            for (const Model3D::RhiDraw &mesh : draws) {
                if (mesh.blended) continue; // its blended surfaces hide nothing
                Place pl = place(*p.item, p.model, cam, fixed);
                const QMatrix4x4 identity;
                std::memcpy(pl.view, identity.constData(), sizeof pl.view);
                pl.ink[0] = ink.x(), pl.ink[1] = ink.y(), pl.ink[2] = ink.z(), pl.ink[3] = ink.w();
                QRhiBuffer *b = d->ubuf();
                u->updateDynamicBuffer(b, 0, sizeof pl, &pl);
                draw.commands.push_back({draw.kind, b, shadowBind(b, p.item->displaceRhi), mesh.vertices, mesh.indices, mesh.indexCount});
            }
        }
    };
    if (reach > 0) {
        for (size_t k = 0; k < spots.size(); ++k) {
            ShadowDraw draw{Kind::ShadowSpot, &d->spotMaps, int(k), {}, QSize(mapSize, mapSize)};
            casters(draw, spots[k].matrix, backendProjection(rhi, spots[k].matrix), {});
            shadowDraws.push_back(std::move(draw));
        }
        static const QVector3D kLook[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        static const QVector3D kUp[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
        for (size_t k = 0; k < points.size(); ++k)
            for (int face = 0; face < 6; ++face) {
                ShadowDraw draw{Kind::ShadowPoint, &d->pointMaps, int(k) * 6 + face, {}, QSize(d->pointMaps.size, d->pointMaps.size)};
                QMatrix4x4 faceView, faceProj;
                faceView.lookAt(points[k].at, points[k].at + kLook[face], kUp[face]);
                faceProj.perspective(90, 1, 0.02f, points[k].far);
                // The camera's view is the identity here: the view is in the matrix
                casters(draw, faceProj * faceView, backendProjection(rhi, faceProj * faceView), QVector4D(points[k].at, points[k].far));
                shadowDraws.push_back(std::move(draw));
            }
    }

    const auto bind = [&](QRhiBuffer *b, const Model3D::RhiDraw &draw, QRhiTexture *displace) {
        QRhiTexture *black = res.black();
        std::vector<RhiResources::Bind> bindings{
            {0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, b},
            {1, QRhiShaderResourceBinding::FragmentStage, d->sceneUbuf.get()},
            {2, QRhiShaderResourceBinding::VertexStage, nullptr, displace ? displace : black, res.linear()},
            {3, QRhiShaderResourceBinding::FragmentStage, nullptr, draw.baseTexture ? draw.baseTexture : black, d->repeat.get()}};
        for (int k = 0; k < 5; ++k)
            bindings.push_back({4 + k, QRhiShaderResourceBinding::FragmentStage, nullptr, draw.maps[size_t(k)] ? draw.maps[size_t(k)] : black, d->repeat.get()});
        bindings.push_back({9, QRhiShaderResourceBinding::FragmentStage, d->lights.get(), nullptr, nullptr, true});
        bindings.push_back({10, QRhiShaderResourceBinding::FragmentStage, d->shadowData.get(), nullptr, nullptr, true});
        bindings.push_back({11, QRhiShaderResourceBinding::FragmentStage, d->pointData.get(), nullptr, nullptr, true});
        bindings.push_back({12, QRhiShaderResourceBinding::FragmentStage, nullptr, d->spotMaps.texture.get(), d->nearest.get()});
        bindings.push_back({13, QRhiShaderResourceBinding::FragmentStage, nullptr, d->pointMaps.texture.get(), d->nearest.get()});
        bindings.push_back({14, QRhiShaderResourceBinding::FragmentStage, nullptr, wantsAo ? d->aoBlurred.color.get() : black, res.linear()});
        bindings.push_back({15, QRhiShaderResourceBinding::FragmentStage, nullptr, wantsSeen ? d->seen.color.get() : black, d->nearest.get()});
        bindings.push_back({16, QRhiShaderResourceBinding::FragmentStage, nullptr, d->lastValid && reflections > 0 && d->last.ready() ? d->last.texture() : black, res.linear()});
        bindings.push_back({17, QRhiShaderResourceBinding::FragmentStage, nullptr, hasEnv ? d->env.texture() : black, d->envSampler.get()});
        return res.bindings(bindings);
    };
    std::vector<Command> commands;
    int triangles = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (size_t order = 0; order < placed.size(); ++order) {
            const Placed &pl = placed[pass == 0 ? order : placed.size() - 1 - order];
            const Scene3D::Item &item = *pl.item;
            const bool pictured = item.pictureRhi != nullptr;
            if (pass == 1 && (pictured || !item.model->hasBlended())) continue;
            std::vector<Model3D::RhiDraw> draws = item.model->rhiDraws(rhi, u, &item.placement, pictured, false);
            for (Model3D::RhiDraw &draw : draws) {
                if (draw.blended != (pass == 1)) continue;
                if (pictured) draw.baseTexture = item.pictureRhi;
                Place p = place(item, pl.model, cam, proj);
                material(p, draw, item.placement.unlit, pictured);
                QRhiBuffer *b = d->ubuf();
                u->updateDynamicBuffer(b, 0, sizeof p, &p);
                commands.push_back({pass == 1 ? Kind::LitBlended : Kind::Lit, b, bind(b, draw, item.displaceRhi), draw.vertices, draw.indices, draw.indexCount});
                if (pass == 0) triangles += draw.indexCount / 3;
            }
        }
    m_triangles = triangles;

    QRhiResourceUpdateBatch *first = res.takeUploads();
    if (first) {
        first->merge(u);
        u->release();
        u = first;
    }
    // What the camera sees, nearest first: the draws of its depth, for the occlusion and the reflections (their numbers go in
    // the batch the first pass carries)
    std::vector<Command> seen;
    if (wantsSeen) {
        for (const Placed &pl : placed) {
            const std::vector<Model3D::RhiDraw> draws = pl.item->model->rhiDraws(rhi, u, &pl.item->placement, pl.item->pictureRhi != nullptr, false);
            for (const Model3D::RhiDraw &mesh : draws) {
                if (mesh.blended) continue; // its blended surfaces hide nothing
                Place pp = place(*pl.item, pl.model, cam, proj);
                QRhiBuffer *b = d->ubuf();
                u->updateDynamicBuffer(b, 0, sizeof pp, &pp);
                seen.push_back({Kind::Depth, b, shadowBind(b, pl.item->displaceRhi), mesh.vertices, mesh.indices, mesh.indexCount});
            }
        }
    }
    // The shadows' passes first: each light's view, a distance or a depth, in its layer
    for (ShadowDraw &draw : shadowDraws) {
        Impl::Maps &maps = *draw.maps;
        QRhiTextureRenderTarget *target = maps.layers[size_t(draw.layer)].target.get();
        cb->beginPass(target, QColor::fromRgbF(1, 1, 1, 1), {1.0f, 0}, u);
        u = nullptr;
        cb->setViewport({0, 0, float(draw.size.width()), float(draw.size.height())});
        for (const Command &c : draw.commands) {
            QRhiGraphicsPipeline *ps = c.srb ? d->shadowPipeline(maps, c.kind, c.srb) : nullptr;
            if (!ps) continue;
            cb->setGraphicsPipeline(ps);
            cb->setViewport({0, 0, float(draw.size.width()), float(draw.size.height())}); // after the pipeline: Vulkan then sets the scissor to it
            cb->setShaderResources(c.srb);
            const QRhiCommandBuffer::VertexInput input(c.vertices, 0);
            cb->setVertexInput(0, 1, &input, c.indices, 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(quint32(c.count));
        }
        cb->endPass();
    }
    // What the camera sees, nearest first: its depth, for the occlusion and the reflections
    if (wantsSeen) {
        cb->beginPass(d->seen.target.get(), QColor::fromRgbF(1, 1, 1, 1), {1.0f, 0}, u);
        u = nullptr;
        cb->setViewport({0, 0, float(w), float(h)});
        for (const Command &c : seen) {
            QRhiGraphicsPipeline *ps = c.srb ? d->pipeline(d->seen, c.kind, c.srb) : nullptr;
            if (!ps) continue;
            cb->setGraphicsPipeline(ps);
            cb->setViewport({0, 0, float(w), float(h)}); // after the pipeline: Vulkan then sets the scissor to it
            cb->setShaderResources(c.srb);
            const QRhiCommandBuffer::VertexInput input(c.vertices, 0);
            cb->setVertexInput(0, 1, &input, c.indices, 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(quint32(c.count));
        }
        cb->endPass();
    }
    if (wantsAo) {
        // Where surfaces meet, at half the size, then without its noise
        const auto fullscreenPass = [&](Impl::Surface &target, const char *fragment, QRhiShaderResourceBindings *srb) {
            const int key = target.id * 100 + 50 + (fragment[8] == 'b' ? 1 : 0);
            std::unique_ptr<QRhiGraphicsPipeline> &ps = d->fullscreen[key];
            if (!ps && srb) {
                ps.reset(rhi->newGraphicsPipeline());
                ps->setShaderStages({{QRhiShaderStage::Vertex, RhiResources::shader(QStringLiteral("scenepost.vert"))},
                                     {QRhiShaderStage::Fragment, RhiResources::shader(QString::fromLatin1(fragment))}});
                ps->setVertexInputLayout({});
                ps->setShaderResourceBindings(srb);
                ps->setRenderPassDescriptor(target.pass.get());
                if (!ps->create()) ps.reset();
            }
            if (!ps) return;
            cb->beginPass(target.target.get(), Qt::white, {1.0f, 0}, u);
            u = nullptr;
            cb->setGraphicsPipeline(ps.get());
            cb->setViewport({0, 0, float(target.size.width()), float(target.size.height())});
            cb->setShaderResources(srb);
            cb->draw(3);
            cb->endPass();
        };
        if (!d->aoUbuf) {
            d->aoUbuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(AoBlock)));
            d->aoUbuf->create();
            d->blurUbuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
            d->blurUbuf->create();
        }
        AoBlock ab = {};
        const QMatrix4x4 invProj = cam.proj.inverted();
        std::memcpy(ab.invProj, invProj.constData(), sizeof ab.invProj);
        std::memcpy(ab.proj, cam.proj.constData(), sizeof ab.proj);
        ab.params[0] = std::max(0.001f, s.aoRadius);
        ab.params[1] = s.ao;
        ab.ints[0] = contactLevel == 1 ? 8 : contactLevel == 2 ? 14 : 24;
        const float texel[4] = {1.0f / float(d->ao.size.width()), 1.0f / float(d->ao.size.height()), 0, 0};
        QRhiResourceUpdateBatch *aoBatch = rhi->nextResourceUpdateBatch();
        aoBatch->updateDynamicBuffer(d->aoUbuf.get(), 0, sizeof ab, &ab);
        aoBatch->updateDynamicBuffer(d->blurUbuf.get(), 0, sizeof texel, texel);
        const auto vs = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        QRhiShaderResourceBindings *aoSrb = res.bindings({{0, vs, d->postUbuf.get()},
                                                          {1, QRhiShaderResourceBinding::FragmentStage, d->aoUbuf.get()},
                                                          {2, QRhiShaderResourceBinding::FragmentStage, nullptr, d->seen.color.get(), d->nearest.get()}});
        QRhiShaderResourceBindings *blurSrb = res.bindings({{0, vs, d->postUbuf.get()},
                                                            {1, QRhiShaderResourceBinding::FragmentStage, d->blurUbuf.get()},
                                                            {2, QRhiShaderResourceBinding::FragmentStage, nullptr, d->seen.color.get(), d->nearest.get()},
                                                            {3, QRhiShaderResourceBinding::FragmentStage, nullptr, d->ao.color.get(), res.linear()}});
        if (u) {
            aoBatch->merge(u);
            u->release();
        }
        u = aoBatch;
        if (aoSrb) fullscreenPass(d->ao, "sceneao.frag", aoSrb);
        if (blurSrb) fullscreenPass(d->aoBlurred, "sceneaoblur.frag", blurSrb);
    }
    const float a = s.background[3];
    const QColor clear = QColor::fromRgbF(s.background[0] * a, s.background[1] * a, s.background[2] * a, a); // premultiplied
    if (wantsGlow) { // the second attachment starts empty: the background goes into the first one alone
        if (!d->backgroundUbuf) {
            d->backgroundUbuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
            d->backgroundUbuf->create();
        }
        const float color[4] = {float(clear.redF()), float(clear.greenF()), float(clear.blueF()), float(clear.alphaF())};
        if (!u) u = rhi->nextResourceUpdateBatch();
        u->updateDynamicBuffer(d->backgroundUbuf.get(), 0, sizeof color, color);
    }
    cb->beginPass(main.target.get(), wantsGlow ? QColor(Qt::transparent) : clear, {1.0f, 0}, u);
    cb->setViewport({0, 0, float(w), float(h)});
    if (wantsGlow) {
        const auto vs = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        QRhiShaderResourceBindings *bgSrb = res.bindings({{0, vs, d->postUbuf.get()}, {1, QRhiShaderResourceBinding::FragmentStage, d->backgroundUbuf.get()}});
        std::unique_ptr<QRhiGraphicsPipeline> &bg = d->fullscreen[main.id * 100 + 70];
        if (!bg && bgSrb) {
            bg.reset(rhi->newGraphicsPipeline());
            bg->setShaderStages({{QRhiShaderStage::Vertex, RhiResources::shader(QStringLiteral("scenepost.vert"))},
                                 {QRhiShaderStage::Fragment, RhiResources::shader(QStringLiteral("scenebackground.frag"))}});
            bg->setVertexInputLayout({});
            bg->setSampleCount(main.samples);
            bg->setTargetBlends({QRhiGraphicsPipeline::TargetBlend(), QRhiGraphicsPipeline::TargetBlend()});
            bg->setShaderResourceBindings(bgSrb);
            bg->setRenderPassDescriptor(main.pass.get());
            if (!bg->create()) bg.reset();
        }
        if (bg && bgSrb) {
            cb->setGraphicsPipeline(bg.get());
            cb->setViewport({0, 0, float(w), float(h)}); // after the pipeline: Vulkan then sets the scissor to it
            cb->setShaderResources(bgSrb);
            cb->draw(3);
        }
    }
    if (hasEnv && s.envShow) { // the environment behind the models: a triangle at the far plane, no depth
        const auto vs = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        QRhiShaderResourceBindings *skySrb = res.bindings({{0, vs, d->postUbuf.get()},
                                                           {1, QRhiShaderResourceBinding::FragmentStage, d->skyUbuf.get()},
                                                           {2, QRhiShaderResourceBinding::FragmentStage, nullptr, d->env.texture(), d->envSampler.get()}});
        std::unique_ptr<QRhiGraphicsPipeline> &sky = d->fullscreen[main.id * 100 + 71];
        if (!sky && skySrb) {
            sky.reset(rhi->newGraphicsPipeline());
            sky->setShaderStages({{QRhiShaderStage::Vertex, RhiResources::shader(QStringLiteral("scenesky.vert"))},
                                  {QRhiShaderStage::Fragment, RhiResources::shader(QString::fromLatin1(main.withBright ? "scenesky.frag" : "scenesky_noglow.frag"))}});
            sky->setVertexInputLayout({});
            sky->setSampleCount(main.samples);
            if (main.withBright) sky->setTargetBlends({QRhiGraphicsPipeline::TargetBlend(), QRhiGraphicsPipeline::TargetBlend()});
            sky->setShaderResourceBindings(skySrb);
            sky->setRenderPassDescriptor(main.pass.get());
            if (!sky->create()) sky.reset();
        }
        if (sky && skySrb) {
            cb->setGraphicsPipeline(sky.get());
            cb->setViewport({0, 0, float(w), float(h)}); // after the pipeline: Vulkan then sets the scissor to it
            cb->setShaderResources(skySrb);
            cb->draw(3);
        }
    }
    for (const Command &c : commands) {
        QRhiGraphicsPipeline *ps = c.srb ? d->pipeline(main, c.kind, c.srb) : nullptr;
        if (!ps) continue;
        cb->setGraphicsPipeline(ps);
        cb->setViewport({0, 0, float(w), float(h)}); // after the pipeline: Vulkan then sets the scissor to it
        cb->setShaderResources(c.srb);
        const QRhiCommandBuffer::VertexInput input(c.vertices, 0);
        cb->setVertexInput(0, 1, &input, c.indices, 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(quint32(c.count));
    }
    cb->endPass();
    if (reflections > 0 && d->last.ready()) { // what a reflection shows next time
        d->lastValid = res.blit(cb, main.color.get(), d->last);
    } else {
        d->lastValid = false;
    }

    // Its glow: what shines, halved and spread again, each step wider, then added back up
    if (wantsGlow) {
        const int levels = std::clamp(quality.glow == 1 ? 3 : quality.glow == 3 ? 6 : 5, 1, 6);
        if (d->glowLevels.size() < 6) {
            d->glowLevels.clear();
            for (int k = 0; k < 6; ++k) d->glowLevels.emplace_back();
            for (int k = 0; k < 6; ++k) d->glowLevels[size_t(k)].id = 20 + k;
        }
        for (int k = 0; k < levels; ++k)
            if (!d->ensure(d->glowLevels[size_t(k)], std::max(1, w >> (k + 1)), std::max(1, h >> (k + 1)), 1, false, QRhiTexture::RGBA16F, false, true))
                return main.color.get();
        QRhiResourceUpdateBatch *glowBatch = rhi->nextResourceUpdateBatch();
        struct GlowPass {
            Impl::Surface *target;
            const char *fragment;
            bool additive;
            QRhiTexture *from;
            QSize fromSize;
            float weight;
            int slot;
        };
        std::vector<GlowPass> passes;
        for (int k = 0; k < levels; ++k) {
            QRhiTexture *from = k == 0 ? main.bright.get() : d->glowLevels[size_t(k - 1)].color.get();
            const QSize fromSize = k == 0 ? QSize(w, h) : d->glowLevels[size_t(k - 1)].size;
            passes.push_back({&d->glowLevels[size_t(k)], "sceneglowdown.frag", false, from, fromSize, 0, k});
        }
        for (int k = levels - 1; k > 0; --k) // the wider a level, the more of it as its size asks
            passes.push_back({&d->glowLevels[size_t(k - 1)], "sceneglowup.frag", true, d->glowLevels[size_t(k)].color.get(),
                              d->glowLevels[size_t(k)].size, std::clamp(s.glowSize, 0.0f, 1.0f), levels + k});
        // Every pass's numbers go in the batch the first pass carries
        for (const GlowPass &gp : passes) {
            if (!d->glowUbuf[gp.slot]) {
                d->glowUbuf[gp.slot].reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
                d->glowUbuf[gp.slot]->create();
            }
            const float data[8] = {1.0f / float(gp.fromSize.width()), 1.0f / float(gp.fromSize.height()), 0, 0, gp.weight, 0, 0, 0};
            glowBatch->updateDynamicBuffer(d->glowUbuf[gp.slot].get(), 0, sizeof data, data);
        }
        for (const GlowPass &gp : passes) {
            const auto vs = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
            QRhiShaderResourceBindings *srb = res.bindings({{0, vs, d->postUbuf.get()},
                                                            {1, QRhiShaderResourceBinding::FragmentStage, d->glowUbuf[gp.slot].get()},
                                                            {2, QRhiShaderResourceBinding::FragmentStage, nullptr, gp.from, res.linear()}});
            const int key = gp.target->id * 100 + 60 + (gp.additive ? 1 : 0);
            std::unique_ptr<QRhiGraphicsPipeline> &ps = d->fullscreen[key];
            if (!ps && srb) {
                ps.reset(rhi->newGraphicsPipeline());
                ps->setShaderStages({{QRhiShaderStage::Vertex, RhiResources::shader(QStringLiteral("scenepost.vert"))},
                                     {QRhiShaderStage::Fragment, RhiResources::shader(QString::fromLatin1(gp.fragment))}});
                ps->setVertexInputLayout({});
                if (gp.additive) {
                    QRhiGraphicsPipeline::TargetBlend b;
                    b.enable = true;
                    b.srcColor = b.srcAlpha = b.dstColor = b.dstAlpha = QRhiGraphicsPipeline::One;
                    ps->setTargetBlends({b});
                }
                ps->setShaderResourceBindings(srb);
                ps->setRenderPassDescriptor(gp.target->pass.get());
                if (!ps->create()) ps.reset();
            }
            if (!ps || !srb) continue;
            cb->beginPass(gp.target->target.get(), Qt::black, {1.0f, 0}, glowBatch);
            glowBatch = nullptr;
            cb->setGraphicsPipeline(ps.get());
            cb->setViewport({0, 0, float(gp.target->size.width()), float(gp.target->size.height())});
            cb->setShaderResources(srb);
            cb->draw(3);
            cb->endPass();
        }
        if (glowBatch) glowBatch->release();
    }

    // The air: walked from the camera at half the size, then without its noise
    if (wantsFog) {
        if (!d->fogUbuf) {
            d->fogUbuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(FogBlock)));
            d->fogUbuf->create();
            d->fogBlurUbuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
            d->fogBlurUbuf->create();
        }
        FogBlock fb = {};
        const QMatrix4x4 invViewProj = (cam.proj * cam.view).inverted();
        std::memcpy(fb.inv, invViewProj.constData(), sizeof fb.inv);
        fb.eye[0] = cam.eye.x(), fb.eye[1] = cam.eye.y(), fb.eye[2] = cam.eye.z();
        fb.color[0] = s.fogColor[0], fb.color[1] = s.fogColor[1], fb.color[2] = s.fogColor[2];
        fb.a[0] = s.fogDensity;
        fb.a[1] = s.fogBase;
        fb.a[2] = std::max(0.0f, s.fogFalloff);
        fb.a[3] = std::max(0.01f, s.fogReach);
        fb.b[0] = s.exposure;
        fb.ints[0] = quality.atmosphere == 1 ? 8 : quality.atmosphere == 3 ? 32 : 16;
        fb.ints[1] = sb.counts[0];
        const float texel[4] = {1.0f / float(d->fogA.size.width()), 1.0f / float(d->fogA.size.height()), 0, 0};
        QRhiResourceUpdateBatch *fogBatch = rhi->nextResourceUpdateBatch();
        fogBatch->updateDynamicBuffer(d->fogUbuf.get(), 0, sizeof fb, &fb);
        fogBatch->updateDynamicBuffer(d->fogBlurUbuf.get(), 0, sizeof texel, texel);
        const auto vs = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        const auto fs = QRhiShaderResourceBinding::FragmentStage;
        QRhiShaderResourceBindings *fogSrb = res.bindings({{0, vs, d->postUbuf.get()},
                                                           {1, fs, d->fogUbuf.get()},
                                                           {2, fs, nullptr, d->seen.color.get(), d->nearest.get()},
                                                           {9, fs, d->lights.get(), nullptr, nullptr, true},
                                                           {10, fs, d->shadowData.get(), nullptr, nullptr, true},
                                                           {11, fs, d->pointData.get(), nullptr, nullptr, true},
                                                           {12, fs, nullptr, d->spotMaps.texture.get(), d->nearest.get()},
                                                           {13, fs, nullptr, d->pointMaps.texture.get(), d->nearest.get()}});
        QRhiShaderResourceBindings *blurSrb = res.bindings({{0, vs, d->postUbuf.get()},
                                                            {1, fs, d->fogBlurUbuf.get()},
                                                            {2, fs, nullptr, d->seen.color.get(), d->nearest.get()},
                                                            {3, fs, nullptr, d->fogA.color.get(), res.linear()}});
        const auto run = [&](Impl::Surface &target, const char *fragment, QRhiShaderResourceBindings *srb, int key) {
            std::unique_ptr<QRhiGraphicsPipeline> &ps = d->fullscreen[target.id * 100 + key];
            if (!ps && srb) {
                ps.reset(rhi->newGraphicsPipeline());
                ps->setShaderStages({{QRhiShaderStage::Vertex, RhiResources::shader(QStringLiteral("scenepost.vert"))},
                                     {QRhiShaderStage::Fragment, RhiResources::shader(QString::fromLatin1(fragment))}});
                ps->setVertexInputLayout({});
                ps->setShaderResourceBindings(srb);
                ps->setRenderPassDescriptor(target.pass.get());
                if (!ps->create()) ps.reset();
            }
            if (!ps || !srb) return;
            cb->beginPass(target.target.get(), Qt::black, {1.0f, 0}, fogBatch);
            fogBatch = nullptr;
            cb->setGraphicsPipeline(ps.get());
            cb->setViewport({0, 0, float(target.size.width()), float(target.size.height())});
            cb->setShaderResources(srb);
            cb->draw(3);
            cb->endPass();
        };
        run(d->fogA, "scenefog.frag", fogSrb, 80);
        run(d->fogB, "scenefogblur.frag", blurSrb, 81);
        if (fogBatch) fogBatch->release();
    }

    // All over the picture, as a camera does: the glow, the colours, the darker edges
    const bool graded = s.vignette > 0 || s.contrast != 1 || s.saturation != 1 || s.temperature != 0;
    if (!graded && !wantsGlow && !wantsFog) return main.color.get();
    if (!d->ensure(d->graded, w, h, 1, false)) return main.color.get();
    QRhiResourceUpdateBatch *post = rhi->nextResourceUpdateBatch();
    pb.more[0] = wantsGlow ? 1.0f : 0.0f;
    pb.more[1] = s.glow;
    pb.more[2] = wantsFog ? 1.0f : 0.0f;
    post->updateDynamicBuffer(d->postUbuf.get(), 0, sizeof pb, &pb);
    QRhiShaderResourceBindings *srb = res.bindings({{0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, d->postUbuf.get()},
                                                    {1, QRhiShaderResourceBinding::FragmentStage, nullptr, main.color.get(), res.linear()},
                                                    {2, QRhiShaderResourceBinding::FragmentStage, nullptr, wantsGlow ? d->glowLevels[0].color.get() : res.black(), res.linear()},
                                                    {3, QRhiShaderResourceBinding::FragmentStage, nullptr, wantsFog ? d->fogB.color.get() : res.black(), res.linear()}});
    if (!d->postPipeline && srb) {
        d->postPipeline.reset(rhi->newGraphicsPipeline());
        d->postPipeline->setShaderStages({{QRhiShaderStage::Vertex, RhiResources::shader(QStringLiteral("scenepost.vert"))},
                                          {QRhiShaderStage::Fragment, RhiResources::shader(QStringLiteral("scenepost.frag"))}});
        d->postPipeline->setVertexInputLayout({});
        d->postPipeline->setShaderResourceBindings(srb);
        d->postPipeline->setRenderPassDescriptor(d->graded.pass.get());
        if (!d->postPipeline->create()) d->postPipeline.reset();
    }
    if (!d->postPipeline || !srb) return main.color.get();
    cb->beginPass(d->graded.target.get(), Qt::transparent, {1.0f, 0}, post);
    cb->setGraphicsPipeline(d->postPipeline.get());
    cb->setViewport({0, 0, float(w), float(h)});
    cb->setShaderResources(srb);
    cb->draw(3);
    cb->endPass();
    return d->graded.color.get();
}
