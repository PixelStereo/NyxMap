#include "RhiVideoTexture.h"
#include "RhiTarget.h"
#include "VideoBudget.h"
#include "RhiDevice.h"
#include "RhiVideoSurface.h"

extern "C" {
#include <libavutil/frame.h>
}

#include <QFile>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cstring>

// A shader of the engine, baked at build time (qt_add_shaders, prefix /shaders)
static QShader engineShader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}

static bool compressed(VideoLayout::Texels t) { return t != VideoLayout::Texels::U8 && t != VideoLayout::Texels::U16; }

// The texture format of a plane (a plane of three channels: one channel, three times as wide); false: none
static bool planeFormat(const VideoLayout::Plane &p, QRhiTexture::Format *f, QSize *size)
{
    *size = QSize(p.width, p.height);
    switch (p.texels) {
    case VideoLayout::Texels::Dxt1: *f = QRhiTexture::BC1; return true;
    case VideoLayout::Texels::Dxt5: *f = QRhiTexture::BC3; return true;
    case VideoLayout::Texels::Rgtc1: *f = QRhiTexture::BC4; return true;
    case VideoLayout::Texels::Bc7: *f = QRhiTexture::BC7; return true;
    case VideoLayout::Texels::Bc6u: *f = QRhiTexture::BC6H; return true;
    case VideoLayout::Texels::Bc6s: return false; // signed BC6H: no QRhi format
    case VideoLayout::Texels::U8:
    case VideoLayout::Texels::U16: break;
    }
    const bool wide = p.texels == VideoLayout::Texels::U16;
    switch (p.channels) {
    case 1: *f = wide ? QRhiTexture::R16 : QRhiTexture::R8; return true;
    case 2: *f = wide ? QRhiTexture::RG16 : QRhiTexture::RG8; return true;
    case 4:
        if (!wide) {
            *f = QRhiTexture::RGBA8;
            return true;
        }
        [[fallthrough]]; // no 16-bit RGBA format: laid out as one channel, as three channels are
    case 3:
        *f = wide ? QRhiTexture::R16 : QRhiTexture::R8;
        size->setWidth(p.width * p.channels);
        return true;
    default: return false;
    }
}

struct RhiVideoTexture::Impl {
    QRhi *rhi = nullptr;
    RhiVideoSurface surface;
    std::unique_ptr<QRhiTexture> planes[4]; // uploaded planes
    VideoLayout::Plane planeShape[4];
    std::unique_ptr<QRhiTexture> out;
    std::unique_ptr<QRhiTextureRenderTarget> rt;
    std::unique_ptr<QRhiRenderPassDescriptor> rp;
    std::unique_ptr<QRhiBuffer> ubuf;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    std::unique_ptr<QRhiGraphicsPipeline> ps;
    QRhiTexture *bound[4] = {nullptr, nullptr, nullptr, nullptr}; // what srb binds
    uint64_t uploads = 0, surfaced = 0;

    explicit Impl(QRhi *r) : rhi(r), surface(r) {}

    bool ensureTarget(int w, int h)
    {
        const QRhiTexture::Format format = RhiTarget::workingFormat(); // the pictures' precision (8, or 10 bits as half floats)
        if (out && out->pixelSize() == QSize(w, h) && out->format() == format) return true;
        ps.reset();
        srb.reset();
        rp.reset();
        rt.reset();
        out.reset(rhi->newTexture(format, QSize(w, h), 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!out->create()) {
            out.reset();
            return false;
        }
        rt.reset(rhi->newTextureRenderTarget({out.get()}));
        rp.reset(rt->newCompatibleRenderPassDescriptor());
        rt->setRenderPassDescriptor(rp.get());
        return rt->create();
    }

    size_t planeBytes[4] = {0, 0, 0, 0};
    void releasePlane(int p)
    {
        if (planeBytes[p]) videoBudget::removeGpu(videoBudget::Gpu::VideoPlanes, planeBytes[p]);
        planeBytes[p] = 0;
        planes[p].reset();
    }
    ~Impl()
    {
        for (int p = 0; p < 4; ++p) releasePlane(p);
    }

    bool ensurePlane(int p, const VideoLayout::Plane &shape)
    {
        QRhiTexture::Format f;
        QSize size;
        if (!planeFormat(shape, &f, &size) || !rhi->isTextureFormatSupported(f)) return false;
        if (planes[p] && planes[p]->format() == f && planes[p]->pixelSize() == size) return true;
        releasePlane(p);
        planes[p].reset(rhi->newTexture(f, size));
        planeShape[p] = shape;
        if (!planes[p]->create()) {
            planes[p].reset();
            return false;
        }
        // Counted in the graphics memory report: the texels' bytes (compressed: the blocks')
        planeBytes[p] = shape.size ? shape.size : size_t(shape.width) * size_t(shape.height) * size_t(shape.channels) *
                                                      (shape.texels == VideoLayout::Texels::U16 ? 2 : 1);
        videoBudget::addGpu(videoBudget::Gpu::VideoPlanes, planeBytes[p]);
        return true;
    }

    bool ensurePipeline(QRhiTexture *const tex[4])
    {
        const bool same = srb && std::equal(tex, tex + 4, bound);
        if (!same) {
            if (!srb) srb.reset(rhi->newShaderResourceBindings());
            const auto all = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
            const auto fs = QRhiShaderResourceBinding::FragmentStage;
            srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, all, ubuf.get()),
                              QRhiShaderResourceBinding::sampledTexture(1, fs, tex[0], sampler.get()),
                              QRhiShaderResourceBinding::sampledTexture(2, fs, tex[1], sampler.get()),
                              QRhiShaderResourceBinding::sampledTexture(3, fs, tex[2], sampler.get()),
                              QRhiShaderResourceBinding::sampledTexture(4, fs, tex[3], sampler.get())});
            if (!srb->create()) return false;
            std::copy(tex, tex + 4, bound);
        }
        if (ps) return true;
        static const QShader vs = engineShader(QStringLiteral("videoconvert.vert")),
                             fsh = engineShader(QStringLiteral("videoconvert.frag"));
        if (!vs.isValid() || !fsh.isValid()) return false;
        ps.reset(rhi->newGraphicsPipeline());
        ps->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fsh}});
        ps->setShaderResourceBindings(srb.get());
        ps->setRenderPassDescriptor(rp.get());
        return ps->create();
    }
};

RhiVideoTexture::RhiVideoTexture(QRhi *rhi) : d(std::make_unique<Impl>(rhi))
{
    d->ubuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 20 * 16));
    d->sampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                     QRhiSampler::ClampToEdge));
    d->ubuf->create();
    d->sampler->create();
}

RhiVideoTexture::~RhiVideoTexture() = default;

bool RhiVideoTexture::upload(QRhiCommandBuffer *cb, const VideoFrame &f)
{
    if (!f.layout || !cb) return false;
    const VideoLayout &L = *f.layout;
    if (L.planeCount < 1 || L.planeCount > 4 || !d->ensureTarget(L.width, L.height)) return false;
    const bool hap = L.mode >= VideoLayout::Mode::HapRgb;
    QRhiResourceUpdateBatch *u = d->rhi->nextResourceUpdateBatch();
    QRhiTexture *planeTex[4] = {nullptr, nullptr, nullptr, nullptr};
    bool surfaced = false;
    if (L.surface) { // on the GPU already
        surfaced = d->surface.take(cb, f);
        if (!surfaced) {
            u->release();
            return false;
        }
        for (int p = 0; p < L.planeCount; ++p) planeTex[p] = d->surface.plane(p);
    } else {
        if (f.staging >= 0) { // upload buffers are OpenGL's (the QRhi renderer hands none to the decoders)
            u->release();
            return false;
        }
        for (int p = 0; p < L.planeCount; ++p) {
            const VideoLayout::Plane &pl = L.plane[p];
            if (!d->ensurePlane(p, pl)) {
                u->release();
                return false;
            }
            // From the decoder's own frame (rows of its own length) or packed at the layout's offsets
            const bool own = f.bytes.empty() && f.av && !compressed(pl.texels);
            const uint8_t *data = own ? f.av->data[p] : f.bytes.data() + pl.offset;
            const size_t stride = own ? size_t(f.av->linesize[p]) : pl.rowBytes;
            if (!data || (!own && f.bytes.size() < pl.offset + pl.size)) {
                u->release();
                return false;
            }
            QRhiTextureSubresourceUploadDescription sub;
            if (compressed(pl.texels)) {
                sub = QRhiTextureSubresourceUploadDescription(data, quint32(pl.size));
            } else {
                sub = QRhiTextureSubresourceUploadDescription(data, quint32(stride * size_t(pl.height)));
                sub.setDataStride(quint32(stride));
            }
            u->uploadTexture(d->planes[p].get(), QRhiTextureUploadDescription({0, 0, sub}));
            planeTex[p] = d->planes[p].get();
        }
    }

    // Each output channel's texture and what it reads
    float c[80] = {};
    QRhiTexture *tex[4];
    for (int k = 0; k < 4; ++k) {
        const int plane = hap ? std::min(k, L.planeCount - 1) : (L.srcPlane[k] >= 0 ? L.srcPlane[k] : 0);
        tex[k] = planeTex[plane];
        if (!hap && L.srcPlane[k] >= 0) {
            c[k * 4 + std::clamp(L.srcChannel[k], 0, 3)] = 1;
            const VideoLayout::Plane &pl = L.plane[plane];
            if (!L.surface && (pl.channels == 3 || (pl.channels == 4 && pl.texels == VideoLayout::Texels::U16)))
                c[32 + k] = float(pl.channels); // laid out as one channel
            if (L.mode == VideoLayout::Mode::Yuv && (k == 1 || k == 2)) {
                const VideoLayout::Plane &pl = L.plane[plane];
                c[16 + k * 4] = L.chromaShift[0] / float(std::max(1, pl.width));
                c[16 + k * 4 + 1] = L.chromaShift[1] / float(std::max(1, pl.height));
            }
        }
    }
    std::copy(L.scale, L.scale + 4, &c[36]);
    for (int r = 0; r < 3; ++r) std::copy(L.matrix + r * 3, L.matrix + r * 3 + 3, &c[40 + r * 4]);
    c[52] = L.yOffset, c[53] = L.yScale, c[54] = L.cOffset, c[55] = L.cScale;
    const VideoLayout::Plane &p0 = L.plane[0];
    c[56] = float(L.width) / float(std::max(1, p0.width));
    c[57] = float(L.height) / float(std::max(1, p0.height));
    // HAP's textures are rounded up to 4: never sampled beyond the picture's last texel centre
    c[58] = hap ? (float(L.width) - 0.5f) / float(std::max(1, p0.width)) : 1.0f;
    c[59] = hap ? (float(L.height) - 0.5f) / float(std::max(1, p0.height)) : 1.0f;
    int mode = 0;
    switch (L.mode) {
    case VideoLayout::Mode::Yuv: mode = 0; break;
    case VideoLayout::Mode::Rgb: mode = 1; break;
    case VideoLayout::Mode::Gray: mode = 2; break;
    case VideoLayout::Mode::HapRgb: mode = 3; break;
    case VideoLayout::Mode::HapYCoCg: mode = 4; break;
    case VideoLayout::Mode::HapYCoCgAlpha: mode = 5; break;
    case VideoLayout::Mode::HapAlphaOnly: mode = 6; break;
    }
    c[60] = float(mode);
    c[61] = L.alpha ? 1.0f : 0.0f;
    c[62] = rhidevice::ndcFlip(d->rhi);
    if (surfaced) std::fill(d->bound, d->bound + 4, nullptr); // a surface's textures are new objects: bound again
    if (!d->ensurePipeline(tex)) {
        u->release();
        return false;
    }
    u->updateDynamicBuffer(d->ubuf.get(), 0, sizeof c, c);
    cb->beginPass(d->rt.get(), Qt::black, {1.0f, 0}, u);
    cb->setGraphicsPipeline(d->ps.get());
    cb->setViewport({0, 0, float(L.width), float(L.height)});
    cb->setShaderResources();
    cb->draw(3);
    cb->endPass();
    ++d->uploads;
    if (surfaced) ++d->surfaced;
    return true;
}

QRhiTexture *RhiVideoTexture::texture() const { return d->out.get(); }
int RhiVideoTexture::width() const { return d->out ? d->out->pixelSize().width() : 0; }
int RhiVideoTexture::height() const { return d->out ? d->out->pixelSize().height() : 0; }
uint64_t RhiVideoTexture::uploads() const { return d->uploads; }
uint64_t RhiVideoTexture::surfaceUploads() const { return d->surfaced; }
