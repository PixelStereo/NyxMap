// IsfInstance's QRhi renderer (Isf.h): the shader as rhiSourcesOf writes it, baked for the QRhi's backend, and the
// passes, targets, images and points of render() drawn through QRhi. The state (inputs, time, frame count) is the
// instance's own.
#include "Isf.h"
#include "ImageFile.h"
#include "ImageUtil.h"
#include "RhiResources.h"
#include "RhiTarget.h"

#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <rhi/qrhi.h>
#include <rhi/qshaderbaker.h>

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>

namespace {
// Baked shaders are kept by their source (the engine's instance of a shader checks it when it loads it, the renderer's
// instance bakes it again): the same text is baked once. Errors are kept too
struct Baked {
    QShader shader;
    QString error;
};
QShader bake(const QString &src, QShader::Stage stage, QString *err)
{
    static std::mutex mutex;
    static std::map<std::pair<QString, int>, Baked> cache;
    const std::pair<QString, int> key{src, int(stage)};
    {
        std::lock_guard<std::mutex> lk(mutex);
        if (const auto found = cache.find(key); found != cache.end()) {
            if (!found->second.shader.isValid() && err) *err += found->second.error;
            return found->second.shader;
        }
    }
    QShaderBaker b;
    b.setSourceString(src.toUtf8(), stage);
    b.setGeneratedShaders({{QShader::SpirvShader, QShaderVersion(100)},
                           {QShader::MslShader, QShaderVersion(20)},
                           {QShader::HlslShader, QShaderVersion(50)}});
    b.setGeneratedShaderVariants({QShader::StandardShader});
    const QShader s = b.bake();
    const QString error = s.isValid() ? QString() : b.errorMessage();
    if (!s.isValid() && err) *err += error;
    std::lock_guard<std::mutex> lk(mutex);
    if (cache.size() > 512) cache.clear();
    cache[key] = {s, error};
    return s;
}

QRhiTexture *loadImage(QRhi *rhi, const QString &path, QRhiResourceUpdateBatch *u, QSize *size)
{
    QImage img = readImageFile(path);
    if (img.isNull()) return nullptr;
    img = prepareImageUpload(img).convertToFormat(QImage::Format_RGBA8888); // first row: the bottom
    QRhiTexture *t = rhi->newTexture(QRhiTexture::RGBA8, img.size());
    if (!t->create()) {
        delete t;
        return nullptr;
    }
    u->uploadTexture(t, img);
    *size = img.size();
    return t;
}
} // namespace

struct IsfInstance::RhiState {
    QRhi *rhi = nullptr;
    RhiSources src;
    QShader vs, vsPoints, fs;
    bool valid = false;
    // Where each member of the block takes its value from
    enum class From { PassIndex, RenderSize, Time, TimeDelta, Date, FrameIndex, Flip, Input, ImageSize, ImageRect, None };
    struct Member {
        From from = From::None;
        int offset = 0, kind = 0, index = -1; // index: the input, or the sampler (ImageSize, ImageRect)
    };
    std::vector<Member> members;
    // The samplers in binding order and what each shows
    enum class Kind { InputImage, Input, Target, Imported, Extra };
    struct Sampler {
        Kind kind = Kind::Input;
        int index = -1; // the input, target or imported image
    };
    std::vector<Sampler> samplers;
    std::map<QString, QRhiTexture *> extras; // pictures of the engine for samplers the shader declares itself
    // Images of the inputs (loaded from their file) and imported images
    struct Image {
        QString path;
        std::unique_ptr<QRhiTexture> tex;
        QSize size;
    };
    std::vector<Image> inputImages, imported;
    std::vector<std::array<RhiTarget, 2>> targets;
    std::vector<int> front;
    std::vector<std::unique_ptr<QRhiBuffer>> ubufs; // one per pass
    std::map<int, std::unique_ptr<QRhiGraphicsPipeline>> pipelines; // by format, points and blending
    QRhiResourceUpdateBatch *pending = nullptr;      // images to upload with the next pass

    ~RhiState()
    {
        if (pending) pending->release();
    }
};

void IsfInstance::RhiStateDeleter::operator()(RhiState *s) const { delete s; }

void IsfInstance::releaseRhi()
{
    if (m_rhi && m_rhi->rhi && !m_rhi->rhi->isDeviceLost()) m_rhi->rhi->finish();
    m_rhi.reset();
}

void IsfInstance::setExtraRhiTexture(const QString &name, QRhiTexture *tex)
{
    if (m_rhi) m_rhi->extras[name] = tex;
}

bool IsfInstance::checkShaders(const RhiSources &src, QString *err)
{
    QString e;
    const bool ok = bake(src.vertex, QShader::VertexStage, &e).isValid() && bake(src.fragment, QShader::FragmentStage, &e).isValid();
    if (!ok && err) *err = QStringLiteral("Compilation error:\n") + e;
    return ok;
}

bool IsfInstance::isValidRhi() const { return m_rhi && m_rhi->valid; }

bool IsfInstance::loadRhi(const QString &path, QRhi *rhi)
{
    m_rhi.reset(new RhiState);
    m_inputs.clear();
    m_passes.clear();
    m_targets.clear();
    m_imported.clear();
    m_error.clear();
    m_isFilter = false;
    m_canSkipDraw = false;
    m_time = 0;
    m_frameIndex = 0;
    RhiState &r = *m_rhi;
    r.rhi = rhi;
    QString fsBody, vsBody;
    if (!readSources(path, &fsBody, &vsBody)) return false;
    r.src = rhiSourcesOf(fsBody, vsBody);
    QString err;
    r.vs = bake(r.src.vertex, QShader::VertexStage, &err);
    r.vsPoints = bake(r.src.vertexPoints, QShader::VertexStage, &err);
    r.fs = bake(r.src.fragment, QShader::FragmentStage, &err);
    if (!r.vs.isValid() || !r.fs.isValid()) {
        m_error = QStringLiteral("Compilation error:\n") + err;
        return false;
    }

    // Each sampler: an input (the filter's picture or another), a target, an imported image (rhiSourcesOf's order)
    for (const QString &name : r.src.samplers) {
        RhiState::Sampler smp;
        for (int i = 0; i < int(m_inputs.size()); ++i)
            if (m_inputs[size_t(i)].name == name) smp = {m_inputs[size_t(i)].isInputImage ? RhiState::Kind::InputImage : RhiState::Kind::Input, i};
        for (int i = 0; i < int(m_targets.size()); ++i)
            if (m_targets[size_t(i)].name == name) smp = {RhiState::Kind::Target, i};
        for (int i = 0; i < int(m_imported.size()); ++i)
            if (m_imported[size_t(i)].name == name) smp = {RhiState::Kind::Imported, i};
        if (name == QLatin1String("nyxmapSegments")) smp = {RhiState::Kind::Extra, -1};
        r.samplers.push_back(smp);
    }
    for (const RhiSources::Uniform &u : r.src.uniforms) {
        RhiState::Member m;
        m.offset = u.offset;
        m.kind = int(u.kind);
        if (u.name == QLatin1String("PASSINDEX")) m.from = RhiState::From::PassIndex;
        else if (u.name == QLatin1String("RENDERSIZE")) m.from = RhiState::From::RenderSize;
        else if (u.name == QLatin1String("TIME")) m.from = RhiState::From::Time;
        else if (u.name == QLatin1String("TIMEDELTA")) m.from = RhiState::From::TimeDelta;
        else if (u.name == QLatin1String("DATE")) m.from = RhiState::From::Date;
        else if (u.name == QLatin1String("FRAMEINDEX")) m.from = RhiState::From::FrameIndex;
        else if (u.name == QLatin1String("isf_ndcFlip")) m.from = RhiState::From::Flip;
        else if (u.name.startsWith(QLatin1Char('_'))) {
            for (int s = 0; s < r.src.samplers.size(); ++s) {
                if (u.name == QStringLiteral("_%1_imgSize").arg(r.src.samplers[s])) m = {RhiState::From::ImageSize, u.offset, int(u.kind), s};
                if (u.name == QStringLiteral("_%1_imgRect").arg(r.src.samplers[s])) m = {RhiState::From::ImageRect, u.offset, int(u.kind), s};
            }
        } else {
            for (int i = 0; i < int(m_inputs.size()); ++i)
                if (m_inputs[size_t(i)].name == u.name) m = {RhiState::From::Input, u.offset, int(u.kind), i};
        }
        r.members.push_back(m);
    }
    // Imported images now; the inputs' own at their first render (setImageInput may change them)
    r.inputImages.resize(m_inputs.size());
    r.imported.resize(m_imported.size());
    r.pending = rhi->nextResourceUpdateBatch();
    for (size_t i = 0; i < m_imported.size(); ++i) {
        RhiState::Image &img = r.imported[i];
        img.path = m_imported[i].path;
        img.tex.reset(loadImage(rhi, img.path, r.pending, &img.size));
        if (!img.tex) m_error += QStringLiteral("Unreadable image: ") + img.path + QLatin1Char('\n');
    }
    r.targets.resize(m_targets.size());
    r.front.assign(m_targets.size(), 0);
    const size_t passes = std::max<size_t>(1, m_passes.size());
    for (size_t i = 0; i < passes; ++i) {
        r.ubufs.emplace_back(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, quint32(std::max(16, r.src.blockSize))));
        if (!r.ubufs.back()->create()) return false;
    }
    static const QRegularExpression discard(QStringLiteral("\\bdiscard\\b"));
    m_canSkipDraw = vsBody.isEmpty() && m_targets.empty()
                    && (m_passes.empty() || (m_passes.size() == 1 && m_passes.front().pointsExpr.isEmpty()))
                    && !fsBody.contains(discard);
    r.valid = true;
    return true;
}

void IsfInstance::renderRhi(QRhiCommandBuffer *cb, const IsfRenderContext &rc, QRhiTexture *input, RhiTarget &out,
                            QSize outSize, bool repeatFrame)
{
    if (!m_rhi) return;
    RhiState &r = *m_rhi;
    QRhi *rhi = r.rhi;
    RhiResources &res = RhiResources::of(rhi);
    const QRhiTexture::Format plain = out.ready() && out.format() == QRhiTexture::RGBA16F ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8;
    out.ensure(rhi, outSize, plain);
    if (!out.ready()) return;
    if (!r.valid) {
        // Broken shader: the picture through (filter), or black (generator)
        if (input) res.blit(cb, input, out);
        else res.clear(cb, out);
        return;
    }
    QRhiResourceUpdateBatch *u = r.pending ? r.pending : rhi->nextResourceUpdateBatch();
    r.pending = nullptr;
    if (QRhiResourceUpdateBatch *shared = res.takeUploads()) u->merge(shared), shared->release();

    // The inputs' images: loaded when their file changed
    for (size_t i = 0; i < m_inputs.size(); ++i) {
        const IsfInput &in = m_inputs[i];
        RhiState::Image &img = r.inputImages[i];
        if (in.type != IsfInput::Image || in.isInputImage || img.path == in.imagePath) continue;
        img.path = in.imagePath;
        img.tex.reset(in.imagePath.isEmpty() ? nullptr : loadImage(rhi, in.imagePath, u, &img.size));
    }

    bool restart = false;
    if (m_restartPending) { // from its first frame: its time, its frame count and what its buffers kept
        m_restartPending = false;
        m_time = 0;
        m_frameIndex = 0;
        restart = true;
    }
    const double dt = repeatFrame ? 0.0 : rc.dt * speed;
    if (!repeatFrame) m_time += (rc.elapsed >= 0 ? rc.elapsed : rc.dt) * speed;
    const int frameIndex = repeatFrame ? std::max(0, m_frameIndex - 1) : m_frameIndex;

    // A sampler's picture and size (GL: a missing picture is black, 1 × 1)
    auto picture = [&](int s, QSize *size) -> QRhiTexture * {
        const RhiState::Sampler &smp = r.samplers[size_t(s)];
        QRhiTexture *t = nullptr;
        QSize sz;
        switch (smp.kind) {
        case RhiState::Kind::InputImage:
            t = input;
            if (t) sz = t->pixelSize();
            break;
        case RhiState::Kind::Input: {
            if (smp.index < 0) break;
            const IsfInput &in = m_inputs[size_t(smp.index)];
            if (in.rhiExt) t = in.rhiExt, sz = QSize(in.extW, in.extH);
            else if (r.inputImages[size_t(smp.index)].tex) t = r.inputImages[size_t(smp.index)].tex.get(), sz = r.inputImages[size_t(smp.index)].size;
            break;
        }
        case RhiState::Kind::Target: {
            const RhiTarget &ft = r.targets[size_t(smp.index)][size_t(r.front[size_t(smp.index)])];
            if (ft.ready()) t = ft.texture(), sz = ft.size();
            break;
        }
        case RhiState::Kind::Extra:
            if (auto it = r.extras.find(r.src.samplers[size_t(s)]); it != r.extras.end() && it->second)
                t = it->second, sz = t->pixelSize();
            break;
        case RhiState::Kind::Imported:
            if (smp.index >= 0 && r.imported[size_t(smp.index)].tex)
                t = r.imported[size_t(smp.index)].tex.get(), sz = r.imported[size_t(smp.index)].size;
            break;
        }
        *size = t ? sz : QSize(1, 1);
        return t ? t : res.black();
    };

    static const std::vector<IsfPass> kSinglePass{IsfPass{}};
    const std::vector<IsfPass> &passes = m_passes.empty() ? kSinglePass : m_passes;
    const int outW = out.width(), outH = out.height();
    std::vector<char> block(size_t(std::max(16, r.src.blockSize)));
    for (size_t i = 0; i < passes.size(); ++i) {
        const IsfPass &pass = passes[i];
        const bool last = i + 1 == passes.size();
        const int pw = int(std::lround(std::clamp(evalSize(pass.widthExpr, outW, outH, outW), 1.0, 16384.0)));
        const int ph = int(std::lround(std::clamp(evalSize(pass.heightExpr, outW, outH, outH), 1.0, 16384.0)));
        const int points = pass.pointsExpr.isEmpty() ? -1
                                                     : int(std::lround(std::clamp(evalSize(pass.pointsExpr, outW, outH, 0), 0.0, 4194304.0)));
        int t = -1;
        if (!pass.target.isEmpty())
            for (size_t k = 0; k < m_targets.size(); ++k)
                if (m_targets[k].name == pass.target) t = int(k);
        if (t >= 0) {
            const QRhiTexture::Format f = m_targets[size_t(t)].isFloat ? QRhiTexture::RGBA32F : plain;
            for (RhiTarget &buf : r.targets[size_t(t)])
                if (buf.ensure(rhi, QSize(pw, ph), f)) res.clear(cb, buf, std::exchange(u, rhi->nextResourceUpdateBatch()));
            if (!r.targets[size_t(t)][0].ready() || !r.targets[size_t(t)][1].ready()) return;
        }
        if (restart) {
            for (auto &pair : r.targets)
                for (RhiTarget &buf : pair) res.clear(cb, buf, std::exchange(u, rhi->nextResourceUpdateBatch()));
            restart = false;
        }
        RhiTarget *dst = &out;
        if (t >= 0) {
            dst = &r.targets[size_t(t)][size_t(1 - r.front[size_t(t)])];
            // Points drawn over the target's picture: it is carried into the buffer they are drawn in
            if (points >= 0 && !pass.clear) res.blit(cb, r.targets[size_t(t)][size_t(r.front[size_t(t)])].texture(), *dst,
                                                     std::exchange(u, rhi->nextResourceUpdateBatch()));
        }

        // The block for this pass
        std::fill(block.begin(), block.end(), 0);
        for (const RhiState::Member &m : r.members) {
            float v[4] = {0, 0, 0, 0};
            switch (m.from) {
            case RhiState::From::PassIndex: v[0] = float(i); break;
            case RhiState::From::RenderSize: v[0] = float(dst->width()), v[1] = float(dst->height()); break;
            case RhiState::From::Time: v[0] = float(m_time); break;
            case RhiState::From::TimeDelta: v[0] = float(dt); break;
            case RhiState::From::Date: {
                const QDateTime now = QDateTime::currentDateTime();
                v[0] = float(now.date().year()), v[1] = float(now.date().month()), v[2] = float(now.date().day());
                v[3] = now.time().msecsSinceStartOfDay() / 1000.0f;
                break;
            }
            case RhiState::From::FrameIndex: v[0] = float(frameIndex); break;
            case RhiState::From::Flip: v[0] = res.ndcFlip(); break;
            case RhiState::From::ImageSize:
            case RhiState::From::ImageRect: {
                QSize sz;
                picture(m.index, &sz);
                if (m.from == RhiState::From::ImageSize) v[0] = float(sz.width()), v[1] = float(sz.height());
                else v[2] = float(sz.width()), v[3] = float(sz.height());
                break;
            }
            case RhiState::From::Input: {
                const IsfInput &in = m_inputs[size_t(m.index)];
                switch (in.type) {
                case IsfInput::Float: v[0] = float(in.fValue); break;
                case IsfInput::Bool: v[0] = in.bValue ? 1 : 0; break;
                case IsfInput::Event: v[0] = !repeatFrame && in.eventFired ? 1 : 0; break;
                case IsfInput::Long: v[0] = float(in.lValue); break;
                case IsfInput::Point2D: v[0] = float(in.pValue.x()), v[1] = float(in.pValue.y()); break;
                case IsfInput::Color: std::copy(in.cValue, in.cValue + 4, v); break;
                default: break;
                }
                break;
            }
            case RhiState::From::None: break;
            }
            char *at = block.data() + m.offset;
            switch (RhiSources::Uniform::Kind(m.kind)) {
            case RhiSources::Uniform::Int:
            case RhiSources::Uniform::Bool: {
                const qint32 n = qint32(std::lround(v[0]));
                std::memcpy(at, &n, 4);
                break;
            }
            case RhiSources::Uniform::Float: std::memcpy(at, v, 4); break;
            case RhiSources::Uniform::Vec2: std::memcpy(at, v, 8); break;
            case RhiSources::Uniform::Vec4: std::memcpy(at, v, 16); break;
            }
        }
        QRhiBuffer *ubuf = r.ubufs[std::min(i, r.ubufs.size() - 1)].get();
        u->updateDynamicBuffer(ubuf, 0, quint32(block.size()), block.data());

        const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        std::vector<RhiResources::Bind> b;
        b.push_back({0, stages, ubuf});
        for (int s = 0; s < int(r.samplers.size()); ++s) {
            QSize sz;
            b.push_back({s + 1, stages, nullptr, picture(s, &sz), res.linear()});
        }
        QRhiShaderResourceBindings *srb = res.bindings(b);
        if (!srb) return;

        // The pipeline: the target's format, a quad or points, their blending
        const int blend = points < 0 ? 0 : pass.alphaBlend ? 2 : 1;
        const int key = int(dst->format()) * 8 + (points >= 0 ? 4 : 0) + blend;
        std::unique_ptr<QRhiGraphicsPipeline> &ps = r.pipelines[key];
        if (!ps) {
            ps.reset(rhi->newGraphicsPipeline());
            if (points < 0) {
                ps->setShaderStages({{QRhiShaderStage::Vertex, r.vs}, {QRhiShaderStage::Fragment, r.fs}});
                QRhiVertexInputLayout layout;
                layout.setBindings({{2 * sizeof(float)}});
                layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});
                ps->setVertexInputLayout(layout);
            } else {
                ps->setShaderStages({{QRhiShaderStage::Vertex, r.vsPoints}, {QRhiShaderStage::Fragment, r.fs}});
                QRhiGraphicsPipeline::TargetBlend tb;
                tb.enable = true;
                tb.srcColor = tb.srcAlpha = QRhiGraphicsPipeline::One;
                tb.dstColor = tb.dstAlpha = blend == 2 ? QRhiGraphicsPipeline::OneMinusSrcAlpha : QRhiGraphicsPipeline::One;
                ps->setTargetBlends({tb});
            }
            ps->setShaderResourceBindings(srb);
            ps->setRenderPassDescriptor(dst->passDescriptor());
            if (!ps->create()) {
                ps.reset();
                return;
            }
        }
        // Points over what the buffer holds unless they clear it; a quad covers it all
        QRhiTextureRenderTarget *rt = points >= 0 && !pass.clear ? dst->kept() : dst->cleared();
        cb->beginPass(rt, Qt::transparent, {1.0f, 0}, u);
        u = rhi->nextResourceUpdateBatch();
        cb->setGraphicsPipeline(ps.get());
        cb->setViewport({0, 0, float(dst->width()), float(dst->height())});
        cb->setShaderResources(srb);
        if (points < 0) {
            const QRhiCommandBuffer::VertexInput vin(res.quad(), 0);
            cb->setVertexInput(0, 1, &vin);
            cb->draw(6);
        } else if (points > 0) {
            cb->draw(quint32(points) * 6);
        }
        cb->endPass();
        if (t >= 0) {
            r.front[size_t(t)] = 1 - r.front[size_t(t)];
            if (last) res.blit(cb, r.targets[size_t(t)][size_t(r.front[size_t(t)])].texture(), out,
                               std::exchange(u, rhi->nextResourceUpdateBatch()));
        }
    }
    u->release();
    if (!repeatFrame) {
        for (IsfInput &in : m_inputs) in.eventFired = false;
        ++m_frameIndex;
    }
}
