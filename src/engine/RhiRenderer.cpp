#include "RhiRenderer.h"

#include "AudioAnalysis.h"
#include "AudioInput.h"
#include "AudioOutput.h"
#include "ColorInfo.h"
#include "Engine.h"
#include "EngineInternal.h"
#include "Isf.h"
#include "Layer.h"
#include "LineGenerator.h"
#include "Mapping.h"
#include "Masks.h"
#include "RhiDevice.h"
#include "RhiResources.h"
#include "RhiScene.h"
#include "RhiVideoTexture.h"
#include "SourceColor.h"
#include "TestCard.h"
#include "TextWriting.h"
#include "VideoDecoder.h"
#include "TimecodeClock.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QFontMetrics>
#include <QRectF>
#include <QWindow>
#include <QSet>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <tuple>
#include <utility>

namespace {
// Each way of drawing, by what tells its pipelines apart
enum class Draw { Composite, CompositeDiff, Prep, EffectMask, OutputColor, Dim, ShapeMask, PictureMatte, Fade, Restore, TestCard };

// The blending of a layer composited (Render.cpp's compositeLayers), premultiplied
QRhiGraphicsPipeline::TargetBlend blendOf(BlendMode mode)
{
    QRhiGraphicsPipeline::TargetBlend b;
    b.enable = true;
    using F = QRhiGraphicsPipeline::BlendFactor;
    switch (mode) {
    case BlendMode::Add: b.srcColor = b.srcAlpha = F::One, b.dstColor = b.dstAlpha = F::One; break;
    case BlendMode::Screen: b.srcColor = b.srcAlpha = F::One, b.dstColor = b.dstAlpha = F::OneMinusSrcColor; break;
    case BlendMode::Multiply: b.srcColor = b.srcAlpha = F::DstColor, b.dstColor = b.dstAlpha = F::OneMinusSrcAlpha; break;
    case BlendMode::Subtract: // dst - src (the layer's premultiplied color), alpha kept
        b.opColor = QRhiGraphicsPipeline::ReverseSubtract;
        b.srcColor = F::One, b.dstColor = F::One, b.srcAlpha = F::Zero, b.dstAlpha = F::One;
        break;
    default: b.srcColor = b.srcAlpha = F::One, b.dstColor = b.dstAlpha = F::OneMinusSrcAlpha; break;
    }
    return b;
}

// Masks are drawn at most 2048 pixels on their long side (their soft edges and the GPU's filtering hide it)
QSize maskRasterSize(QSize s)
{
    const int longSide = std::max(s.width(), s.height());
    if (longSide <= 2048) return QSize(std::max(1, s.width()), std::max(1, s.height()));
    const double k = 2048.0 / longSide;
    return QSize(std::max(1, int(std::lround(s.width() * k))), std::max(1, int(std::lround(s.height() * k))));
}
} // namespace

struct RhiRenderer::Impl {
    Engine &e;
    std::unique_ptr<QRhi> rhi;
    QRhiCommandBuffer *cb = nullptr;
    std::unique_ptr<QRhiBuffer> flip, gridIndices, quadIndices, fullMesh;
    int gridIndexCount = 0;
    std::vector<std::unique_ptr<QRhiBuffer>> ubufs; // 256 bytes each, a new one per draw within a frame
    size_t nextUbuf = 0;
    std::map<int, std::unique_ptr<QRhiGraphicsPipeline>> pipelines; // by draw, blending and target format
    QRhiResourceUpdateBatch *pending = nullptr;                       // uploads for the next pass
    std::set<QString> warned;
    QElapsedTimer cardClock = [] { QElapsedTimer t; t.start(); return t; }(); // the test cards' motion

    struct Effect {
        QString path;
        std::unique_ptr<IsfInstance> isf;
    };
    struct RLayer {
        std::unique_ptr<RhiVideoTexture> video;
        std::unique_ptr<QRhiTexture> image;
        QSize imageSize;
        std::unique_ptr<QRhiTexture> autoImage; // a picture with an uncommon ICC profile, converted to sRGB (the "auto" source colour)
        QSize autoSize;
        std::unique_ptr<QRhiTexture> hdr; // an HDR file's pixels, in floats
        QRhiTexture *incoming = nullptr; // what the layer shows once its source transition is over (the held frame, until it is drawn again)
        std::unique_ptr<QRhiTexture> card; // a viewport's test card, still (TestCard.h)
        QString cardKey;
        std::unique_ptr<QRhiTexture> segments; // a Line generator's lines (LineGenerator.h), two rows of floats
        unsigned segmentsRev = 0;
        QSize segmentsSize;
        std::unique_ptr<IsfInstance> generator;
        QString generatorPath;
        RhiTarget liveTarget; // a live input's texture of another API, turned the right way up
        QSize liveSize;
        RhiTarget generatorTarget, sourceColor, prep, fx[2], mixed[2], group, out;
        RhiTarget mip; // the picture with its mip levels, drawn smaller than it is
        QRhiTexture *mipSource = nullptr;
        quint64 mipSerial = 0, videoSerial = 0;
        int mipKind = 0;
        RhiTarget shapeMasked, mattePicture, matteTarget; // through its surface masks, through a Picture matte, its matte in Composition space
        std::unique_ptr<QRhiTexture> surfaceMask, compositionMask; // their alpha, top row first
        unsigned surfaceMaskRev = 0, compositionMaskRev = 0;
        QSize surfaceMaskSize, compositionMaskSize;
        std::unique_ptr<QRhiTexture> text; // a Text layer's picture, top row first as the images are
        QString textKey;
        std::unique_ptr<IsfInstance> transition; // a source transition's shader (none: a crossfade)
        QString transitionPath;
        RhiTarget transitionTarget;
        std::unique_ptr<RhiScene> scene; // a 3D scene's renderer
        std::map<int, RhiTarget> stageTargets;
        std::vector<Effect> effects;
        QRhiTexture *final = nullptr, *raw = nullptr, *preFx = nullptr;
        std::vector<QRhiTexture *> stage;
        std::unique_ptr<QRhiBuffer> mesh;
        Mapping meshShape;
        bool meshMade = false, meshGrid = false;
        bool used = false;
        int unusedFrames = 0;
    };
    std::map<quint64, RLayer> layers;
    // The output windows: a swapchain each, the viewport's picture drawn into it (present())
    struct Output {
        std::unique_ptr<QRhiSwapChain> sc;
        std::unique_ptr<QRhiRenderPassDescriptor> rp;
        std::unique_ptr<QRhiGraphicsPipeline> ps;
        QWindow *window = nullptr;
        bool resize = true;
        QImage last; // tests: what was drawn, top row first
    };
    std::map<quint64, Output> outputs;
    std::vector<PreviewRequest> previewRequests;
    std::vector<SceneViewRequest> sceneViews;
    std::map<int, std::unique_ptr<RhiScene>> scenes; // by preview slot
    std::vector<PreviewPicture> previewPictures;
    std::map<int, RhiTarget> previewTargets; // by slot
    std::unique_ptr<QRhiBuffer> presentFlip; // the framebuffer's first row is the top of the window: the textures' flip, reversed
    bool capturePresented = false;
    std::map<const Layer *, RLayer> outgoing; // the sources a transition is leaving, by the layer that held them
    RhiTarget compositionOut;
    std::map<std::tuple<int, int, int>, RhiTarget> dstCopies; // what a Difference layer reads: the target so far, by size and format
    std::vector<int> marks;

    explicit Impl(Engine &engine) : e(engine) {}

    void warnOnce(const QString &what)
    {
        if (warned.insert(what).second) qWarning("QRhi renderer: %s not ported yet", qPrintable(what));
    }

    QRhiBuffer *ubuf(const void *data, int bytes)
    {
        if (nextUbuf == ubufs.size()) {
            ubufs.emplace_back(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 256));
            ubufs.back()->create();
        }
        QRhiBuffer *b = ubufs[nextUbuf++].get();
        batch()->updateDynamicBuffer(b, 0, quint32(std::min(bytes, 256)), data);
        return b;
    }
    // A still image's or a live input's picture into the layer's texture
    void uploadPicture(RLayer &r, const QImage &image) { uploadPictureTo(r.image, r.imageSize, image); }
    void uploadPictureTo(std::unique_ptr<QRhiTexture> &texture, QSize &textureSize, const QImage &image)
    {
        // A picture of more than 8 bits (a 16-bit file) keeps them: half floats
        const QPixelFormat pf = image.pixelFormat();
        const bool deep = pf.typeInterpretation() == QPixelFormat::FloatingPoint ||
                          std::max({pf.redSize(), pf.greenSize(), pf.blueSize()}) > 8;
        const bool half = deep && RhiTarget::workingFormat() == QRhiTexture::RGBA16F;
        const QImage rgba = image.convertToFormat(half ? QImage::Format_RGBA16FPx4 : QImage::Format_RGBA8888); // first row: the bottom (prepareImageUpload)
        const QRhiTexture::Format textureFormat = half ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8;
        if (!texture || textureSize != rgba.size() || texture->format() != textureFormat) {
            texture.reset(rhi->newTexture(textureFormat, rgba.size()));
            if (!texture->create()) {
                texture.reset();
                return;
            }
            textureSize = rgba.size();
        }
        batch()->uploadTexture(texture.get(), rgba);
    }
    QRhiResourceUpdateBatch *batch()
    {
        if (!pending) pending = rhi->nextResourceUpdateBatch();
        return pending;
    }
    // The uploads waiting go now (a pass that is not ours, a shader of an ISF, would sample the textures before they hold anything)
    void flushUploads()
    {
        if (pending) cb->resourceUpdate(take());
    }
    QRhiResourceUpdateBatch *take()
    {
        QRhiResourceUpdateBatch *u = batch();
        pending = nullptr;
        if (QRhiResourceUpdateBatch *shared = RhiResources::of(rhi.get()).takeUploads()) u->merge(shared), shared->release();
        return u;
    }
    RhiResources &res() { return RhiResources::of(rhi.get()); }

    // `ms`: for a pass on the target's multisampled buffer (antialiasing)
    QRhiGraphicsPipeline *pipeline(Draw draw, BlendMode blend, const RhiTarget &target, QRhiShaderResourceBindings *srb, bool ms = false)
    {
        const int key = int(draw) * 4096 + int(blend) * 128 + int(target.format()) + (ms ? target.samples() << 20 : 0);
        std::unique_ptr<QRhiGraphicsPipeline> &ps = pipelines[key];
        if (ps) return ps.get();
        ps.reset(rhi->newGraphicsPipeline());
        auto shader = [](const char *name) { return RhiResources::shader(QString::fromLatin1(name)); };
        switch (draw) {
        case Draw::Composite:
        case Draw::CompositeDiff: {
            ps->setShaderStages({{QRhiShaderStage::Vertex, shader("composite.vert")},
                                 {QRhiShaderStage::Fragment, shader(draw == Draw::Composite ? "composite.frag" : "compositediff.frag")}});
            QRhiVertexInputLayout layout;
            layout.setBindings({{4 * sizeof(float)}});
            layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}});
            ps->setVertexInputLayout(layout);
            if (draw == Draw::Composite) ps->setTargetBlends({blendOf(blend)}); // Difference writes its result as it is
            break;
        }
        case Draw::Prep:
        case Draw::EffectMask:
        case Draw::OutputColor:
        case Draw::ShapeMask:
        case Draw::PictureMatte:
        case Draw::Fade:
        case Draw::TestCard:
            ps->setShaderStages({{QRhiShaderStage::Vertex, shader("fullscreen.vert")},
                                 {QRhiShaderStage::Fragment, shader(draw == Draw::Prep ? "prep.frag"
                                                                    : draw == Draw::EffectMask ? "effectmask.frag"
                                                                    : draw == Draw::ShapeMask ? "shapemask.frag"
                                                                    : draw == Draw::PictureMatte ? "matte.frag"
                                                                    : draw == Draw::Fade ? "fade.frag"
                                                                    : draw == Draw::TestCard ? "testcard.frag" : "outputcolor.frag")}});
            break;
        case Draw::Restore: // a picture copied over the whole target (no blending)
            ps->setShaderStages({{QRhiShaderStage::Vertex, shader("fullscreen.vert")}, {QRhiShaderStage::Fragment, shader("blit.frag")}});
            break;
        case Draw::Dim: { // the target times a constant (composition opacity, blackout)
            ps->setShaderStages({{QRhiShaderStage::Vertex, shader("fullscreen.vert")}, {QRhiShaderStage::Fragment, shader("blit.frag")}});
            QRhiGraphicsPipeline::TargetBlend b;
            b.enable = true;
            b.srcColor = b.srcAlpha = QRhiGraphicsPipeline::Zero;
            b.dstColor = b.dstAlpha = QRhiGraphicsPipeline::ConstantColor;
            ps->setTargetBlends({b});
            break;
        }
        }
        ps->setShaderResourceBindings(srb);
        if (ms) ps->setSampleCount(target.samples());
        ps->setRenderPassDescriptor(ms ? target.msPassDescriptor() : target.passDescriptor());
        if (!ps->create()) {
            ps.reset();
            return nullptr;
        }
        return ps.get();
    }

    // A pass drawing one triangle over `target` with a fragment shader of the fullscreen kind
    bool fullscreen(Draw draw, RhiTarget &target, const void *uniforms, int bytes, std::vector<QRhiTexture *> textures,
                    QColor clear = Qt::transparent)
    {
        const auto fs = QRhiShaderResourceBinding::FragmentStage;
        std::vector<RhiResources::Bind> b{{15, QRhiShaderResourceBinding::VertexStage, flip.get()}};
        if (uniforms) b.push_back({0, fs, ubuf(uniforms, bytes)});
        for (size_t k = 0; k < textures.size(); ++k)
            b.push_back({int(k) + 1, fs, nullptr, textures[k] ? textures[k] : res().black(), res().linear()});
        QRhiShaderResourceBindings *srb = res().bindings(b);
        QRhiGraphicsPipeline *ps = srb ? pipeline(draw, BlendMode::Normal, target, srb) : nullptr;
        cb->beginPass(target.cleared(), clear, {1.0f, 0}, take());
        if (ps) {
            cb->setGraphicsPipeline(ps);
            cb->setViewport({0, 0, float(target.width()), float(target.height())});
            cb->setShaderResources(srb);
            cb->draw(3);
        }
        cb->endPass();
        return ps;
    }

    void dim(RhiTarget &target, double level)
    {
        if (level >= 0.999) return;
        std::vector<RhiResources::Bind> b{{15, QRhiShaderResourceBinding::VertexStage, flip.get()},
                                          {1, QRhiShaderResourceBinding::FragmentStage, nullptr, res().black(), res().linear()}};
        QRhiShaderResourceBindings *srb = res().bindings(b);
        QRhiGraphicsPipeline *ps = srb ? pipeline(Draw::Dim, BlendMode::Normal, target, srb) : nullptr;
        cb->beginPass(target.kept(), Qt::transparent, {1.0f, 0}, take());
        if (ps) {
            const float m = float(level);
            cb->setGraphicsPipeline(ps);
            cb->setViewport({0, 0, float(target.width()), float(target.height())});
            cb->setShaderResources(srb);
            cb->setBlendConstants(QColor::fromRgbF(m, m, m, 1.0f));
            cb->draw(3);
        }
        cb->endPass();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Sources
    // ---------------------------------------------------------------------------------------------------------------
    // ISF "audio" and "audioFFT" inputs: the live input's sound or the program's, now, a row per channel, as 32-bit float
    // RGBA (grey), one picture for each kind and width, made once a frame
    struct AudioPicture {
        std::unique_ptr<QRhiTexture> tex;
        quint64 frame = 0, used = 0;
    };
    std::map<std::array<int, 3>, AudioPicture> audioPictures; // program, waveform, columns
    std::vector<float> audioValues, audioRgba;
    QRhiTexture *audioPicture(const IsfInput &in, int *width)
    {
        const quint64 frame = e.m_frameCount.load(std::memory_order_relaxed);
        const bool program = in.audioSource == IsfInput::Program;
        const bool waveform = in.type == IsfInput::Audio;
        const int w = waveform ? AudioAnalysis::waveWidth(in.audioMax) : AudioAnalysis::spectrumWidth(in.audioMax);
        *width = w;
        AudioPicture &entry = audioPictures[{program, waveform, w}];
        entry.used = frame + 1;
        if (!entry.tex || entry.frame != frame + 1) {
            if (!entry.tex || entry.tex->pixelSize() != QSize(w, 2)) {
                entry.tex.reset(rhi->newTexture(QRhiTexture::RGBA32F, QSize(w, 2)));
                if (!entry.tex->create()) entry.tex.reset();
            }
            if (!entry.tex) return nullptr;
            audioValues.resize(size_t(2 * w));
            if (waveform)
                AudioAnalysis::waveColumns(program ? e.m_audio->wave() : e.m_audioInput->wave(), w, audioValues.data());
            else
                AudioAnalysis::spectrumColumns(program ? e.m_audio->spectrum() : e.m_audioInput->spectrum(), w, audioValues.data());
            audioRgba.resize(audioValues.size() * 4);
            for (size_t k = 0; k < audioValues.size(); ++k) {
                audioRgba[4 * k] = audioRgba[4 * k + 1] = audioRgba[4 * k + 2] = audioValues[k];
                audioRgba[4 * k + 3] = 1.f;
            }
            QRhiTextureSubresourceUploadDescription sub(audioRgba.data(), quint32(audioRgba.size() * sizeof(float)));
            sub.setSourceSize(QSize(w, 2));
            batch()->uploadTexture(entry.tex.get(), QRhiTextureUploadDescription({0, 0, sub}));
            entry.frame = frame + 1;
        }
        return entry.tex.get();
    }

    // An ISF instance of this renderer following the engine's one (its values, images, speed); loaded again when the
    // engine's changes file
    void follow(std::unique_ptr<IsfInstance> &mine, QString &path, IsfInstance &theirs)
    {
        if (!mine || path != theirs.path()) {
            path = theirs.path();
            mine = std::make_unique<IsfInstance>();
            if (!mine->loadRhi(path, rhi.get())) qWarning("QRhi renderer: %s: %s", qPrintable(path), qPrintable(mine->error().left(200)));
        }
        mine->speed = theirs.speed;
        if (theirs.takeRestart()) mine->requestRestart();
        auto &dst = mine->inputs();
        const auto &src = theirs.inputs();
        if (dst.size() != src.size()) return;
        for (size_t i = 0; i < src.size(); ++i) {
            IsfInput &d = dst[i];
            const IsfInput &s = src[i];
            d.fValue = s.fValue, d.bValue = s.bValue, d.lValue = s.lValue, d.pValue = s.pValue;
            std::copy(s.cValue, s.cValue + 4, d.cValue);
            d.eventFired = s.eventFired;
            d.imagePath = s.imagePath;
            d.rhiExt = nullptr;
            if (s.imageLayer) { // another layer's picture, after its effects
                const auto found = layers.find(s.imageLayer);
                if (found != layers.end() && found->second.final) {
                    d.rhiExt = found->second.final;
                    d.extW = found->second.final->pixelSize().width();
                    d.extH = found->second.final->pixelSize().height();
                }
            }
            if (s.type == IsfInput::Audio || s.type == IsfInput::AudioFFT) { // the sound now, a row per channel
                int w = 0;
                d.rhiExt = audioPicture(s, &w);
                d.extW = w;
                d.extH = 2;
            }
        }
    }

    void renderLayer(Layer &l, RLayer &r, const IsfRenderContext &rc)
    {
        r.final = r.raw = r.preFx = nullptr;
        if (l.isGroup || l.is3dObject() || l.ended) return;
        if (!l.needed) {
            // A single-pass generator nobody sees keeps its clock (its frame count too), without drawing
            if (l.enabled && l.parentEnabled && !l.soloHidden && l.generator && l.generator->canSkipDraw()) {
                follow(r.generator, r.generatorPath, *l.generator);
                if (r.generator && r.generator->isValidRhi()) {
                    r.generator->advanceWithoutDrawing(rc);
                    l.generator->adoptClock(*r.generator);
                }
            }
            return;
        }
        QRhiTexture *tex = nullptr;
        QSize size;
        switch (l.type) {
        case SourceType::Layer:
            if (const Layer *s = l.sourceLayer ? e.layer(e.indexOfId(l.sourceLayer)) : nullptr) {
                const auto found = layers.find(s->id);
                if (found == layers.end()) break;
                const RLayer &sr = found->second;
                switch (l.sourceTap) {
                case LayerTap::Source: tex = s->isGroup ? sr.final : sr.raw; break;
                case LayerTap::PreFx: tex = sr.preFx; break;
                case LayerTap::PostFx: tex = sr.final; break;
                }
                if (tex) size = tex->pixelSize();
            }
            l.srcWidth = size.width(); // what the inspector and the ROI editor show
            l.srcHeight = size.height();
            break;
        case SourceType::Video:
            if (l.video) {
                if (!r.video) r.video = std::make_unique<RhiVideoTexture>(rhi.get());
                if (!l.frameUploaded && l.frame.layout) {
                    // Its uploads go first in the frame (a pass of their own)
                    r.video->upload(cb, l.frame);
                    ++r.videoSerial;
                    l.frameUploaded = true;
                }
                tex = r.video->texture();
                if (tex) size = tex->pixelSize();
            }
            break;
        case SourceType::Image:
            tex = l.sourceColorSpace == QLatin1String("auto") && r.autoImage ? r.autoImage.get() : r.image.get();
            size = r.imageSize;
            break;
        case SourceType::Isf:
            if (l.generator) {
                follow(r.generator, r.generatorPath, *l.generator);
                if (isLineGeneratorPath(l.sourcePath)) { // its lines as segments, uploaded again when they change
                    if (!r.segments || r.segmentsRev != l.linesRevision || r.segmentsSize != QSize(l.genWidth, l.genHeight)) {
                        int count = 0;
                        const std::vector<float> data = lineSegmentData(l.lines, l.genWidth, l.genHeight, &count);
                        if (!r.segments || r.segments->pixelSize() != QSize(count, 2)) {
                            r.segments.reset(rhi->newTexture(QRhiTexture::RGBA32F, QSize(count, 2)));
                            if (!r.segments->create()) r.segments.reset();
                        }
                        if (r.segments) {
                            QRhiTextureSubresourceUploadDescription sub(data.data(), quint32(data.size() * sizeof(float)));
                            sub.setSourceSize(QSize(count, 2));
                            batch()->uploadTexture(r.segments.get(), QRhiTextureUploadDescription({0, 0, sub}));
                        }
                        r.segmentsRev = l.linesRevision;
                        r.segmentsSize = QSize(l.genWidth, l.genHeight);
                    }
                    r.generator->setExtraRhiTexture(QStringLiteral("nyxmapSegments"), r.segments.get());
                }
                const bool repeatFrame = l.pictureCulled && l.generator->canSkipDraw();
                flushUploads();
                r.generator->renderRhi(cb, rc, nullptr, r.generatorTarget, QSize(l.genWidth, l.genHeight), repeatFrame);
                l.generator->adoptClock(*r.generator);
                if (r.generatorTarget.ready()) tex = r.generatorTarget.texture(), size = r.generatorTarget.size();
            }
            break;
        case SourceType::Text:
            tex = textPicture(l, r);
            if (tex) size = tex->pixelSize();
            break;
        case SourceType::Input:
        case SourceType::Web: { // a live input, or the pictures taken of a web page (pixels, or a texture of another API)
            LiveInput *in = l.type == SourceType::Web ? static_cast<LiveInput *>(l.webPage.get()) : l.input.get();
            if (!in) break;
            auto fit = [&](QSize s) {
                const bool first = !l.srcWidth;
                l.srcWidth = s.width();
                l.srcHeight = s.height();
                if (first && l.type == SourceType::Input && l.mapping.isIdentity() && l.srcHeight > 0)
                    l.mapping.fitAspect(double(l.srcWidth) / l.srcHeight, double(e.m_compSize.width()) / e.m_compSize.height());
            };
            QImage frame;
            if (in->takeFrame(&frame)) {
                fit(frame.size());
                uploadPicture(r, frame);
            }
            QSize shared;
            bool upsideDown = false;
            if (QRhiTexture *t = r.image ? nullptr : in->takeTexture(rhi.get(), &shared, &upsideDown)) {
                if (shared != r.liveSize) {
                    r.liveSize = shared;
                    fit(shared);
                }
                if (!upsideDown) { // as the pictures here are held: used as it is, no copy
                    tex = t;
                    size = shared;
                    break;
                }
                if (!r.liveTarget.ready() || r.liveTarget.size() != shared) r.liveTarget.ensure(rhi.get(), shared);
                if (r.liveTarget.ready() && res().blit(cb, t, r.liveTarget, nullptr, true)) {
                    tex = r.liveTarget.texture();
                    size = shared;
                    break;
                }
            }
            tex = r.image.get();
            size = r.imageSize;
            break;
        }
        case SourceType::Tox: {
            if (!l.toxPlayer) break;
            // The inputs are the source layers' textures; the output a texture of this QRhi's device (a native player), else
            // pixels (a player of another kind)
            for (const auto &top : l.toxPlayer->inputs()) {
                const quint64 source = l.tox.inputs.value(top.first);
                const auto found = e.indexOfId(source) >= 0 ? layers.find(source) : layers.end(); // a deleted source: disconnected
                l.toxPlayer->setInputRhiTexture(top.first, found != layers.end() ? found->second.final : nullptr);
            }
            auto fit = [&](QSize s) {
                const bool first = !l.srcWidth;
                l.srcWidth = s.width();
                l.srcHeight = s.height();
                if (first && l.mapping.isIdentity() && l.srcHeight > 0)
                    l.mapping.fitAspect(double(l.srcWidth) / l.srcHeight, double(e.m_compSize.width()) / e.m_compSize.height());
            };
            bool upsideDown = false;
            QSize shared;
            if (QRhiTexture *t = l.toxPlayer->usesRhi(rhi.get()) ? l.toxPlayer->takeTexture(rhi.get(), &shared, &upsideDown) : nullptr) {
                if (shared != r.liveSize) {
                    r.liveSize = shared;
                    fit(shared);
                }
                if (!upsideDown) {
                    tex = t;
                    size = shared;
                } else {
                    if (!r.liveTarget.ready() || r.liveTarget.size() != shared) r.liveTarget.ensure(rhi.get(), shared);
                    if (r.liveTarget.ready() && res().blit(cb, t, r.liveTarget, nullptr, true)) {
                        tex = r.liveTarget.texture();
                        size = shared;
                    }
                }
                l.error = l.toxPlayer->error();
                break;
            }
            QImage frame;
            if (l.toxPlayer->takeFrame(&frame)) {
                fit(frame.size());
                uploadPicture(r, frame);
            }
            l.error = l.toxPlayer->error();
            tex = r.image.get();
            size = r.imageSize;
            break;
        }
        default: break;
        }
        if (!tex) return;
        const QString colorKey = sourceColorKey(l);
        if (colorKey != QLatin1String("srgb")) { // decoded into sRGB, straight alpha kept
            r.sourceColor.ensure(rhi.get(), size);
            const float u[8] = {1, 1, float(sourceColorSpaceIndex(colorKey)), 0, 1, 0, 0, 0};
            if (!r.sourceColor.ready() || !fullscreen(Draw::OutputColor, r.sourceColor, u, sizeof u, {tex})) return;
            tex = r.sourceColor.texture();
        }
        processLayer(l, r, tex, size, false, rc);
    }

    // Source picture -> ROI and color (when needed) -> effect chain -> its picture (Render.cpp's processLayer)
    void processLayer(Layer &l, RLayer &r, QRhiTexture *tex, QSize size, bool premultiplied, const IsfRenderContext &rc)
    {
        r.raw = tex;
        const int w = size.width(), h = size.height();
        QRectF c = l.roi.normalized() & Layer::fullRoi();
        if (c.width() < 1e-4 || c.height() < 1e-4) c = Layer::fullRoi();
        int roiMode = l.roiMode;
        double cropWidth = w * c.width(), cropHeight = h * c.height();
        const auto &points = l.roiPoints;
        Homography quad;
        if (roiMode) {
            if (!validRoiPolygon(points.data(), roiMode == 1 ? 3 : 4)) roiMode = 3;
            if (roiMode == 1) {
                c = QRectF(QPointF(std::min({points[0].x(), points[1].x(), points[2].x()}),
                                   std::min({points[0].y(), points[1].y(), points[2].y()})),
                           QPointF(std::max({points[0].x(), points[1].x(), points[2].x()}),
                                   std::max({points[0].y(), points[1].y(), points[2].y()})));
                cropWidth = w * c.width();
                cropHeight = h * c.height();
            } else if (roiMode == 2) {
                quad = Homography::squareToQuad(points.data());
                auto edge = [&](int a, int b) {
                    const QPointF dd = points[a] - points[b];
                    return std::hypot(dd.x() * w, dd.y() * h);
                };
                cropWidth = (edge(0, 1) + edge(3, 2)) / 2;
                cropHeight = (edge(0, 3) + edge(1, 2)) / 2;
            }
        }
        const bool restricted = roiMode || c != Layer::fullRoi();
        const ColorAdjust col = l.color.effective();
        QSize cur = size;
        if (restricted || !col.isIdentity() || premultiplied) {
            const QSize crop(std::clamp(int(std::lround(cropWidth)), 1, std::max(1, w)), std::clamp(int(std::lround(cropHeight)), 1, std::max(1, h)));
            r.prep.ensure(rhi.get(), crop);
            if (!r.prep.ready()) {
                r.preFx = r.final = nullptr;
                return;
            }
            float u[40] = {};
            u[0] = float(c.left()), u[1] = float(1.0 - c.bottom()), u[2] = float(c.right()), u[3] = float(1.0 - c.top());
            std::copy(col.add, col.add + 3, &u[4]);
            std::copy(col.remove, col.remove + 3, &u[8]);
            col.balanceGains(&u[12]);
            const Layer *mask = col.maskLayer && !col.isIdentity() ? e.layer(e.indexOfId(col.maskLayer)) : nullptr;
            QRhiTexture *maskTex = nullptr;
            if (mask && mask != &l) {
                const auto found = layers.find(mask->id);
                if (found != layers.end()) maskTex = found->second.final;
            }
            u[16] = float(roiMode), u[17] = premultiplied ? 1.f : 0.f, u[18] = maskTex ? (col.maskInvert ? 2.f : 1.f) : 0.f;
            if (roiMode == 1) {
                const float tri[6] = {float(points[0].x()), float(points[0].y()), float(points[1].x()), float(points[1].y()),
                                      float(points[2].x()), float(points[2].y())};
                std::copy(tri, tri + 6, &u[20]);
            } else if (roiMode == 2) {
                const float m[12] = {float(quad.a), float(quad.d), float(quad.g), 0, float(quad.b), float(quad.e), float(quad.h), 0,
                                     float(quad.c), float(quad.f), 1.f, 0};
                std::copy(m, m + 12, &u[28]);
            }
            if (!fullscreen(Draw::Prep, r.prep, u, sizeof u, {tex, maskTex})) {
                r.preFx = r.final = nullptr;
                return;
            }
            tex = r.prep.texture();
            cur = crop;
        }
        r.preFx = tex;

        if (l.effectsEnabled) {
            int ping = 0;
            r.stage.assign(l.effects.size() + 1, nullptr);
            r.stage[0] = tex;
            r.effects.resize(l.effects.size());
            for (size_t k = 0; k < l.effects.size(); ++k) {
                IsfInstance &theirs = *l.effects[k];
                if (!theirs.enabled) {
                    r.stage[k + 1] = tex;
                    continue;
                }
                Effect &mine = r.effects[k];
                follow(mine.isf, mine.path, theirs);
                QRhiTexture *input = tex;
                RhiTarget &dst = r.fx[ping];
                flushUploads(); // the pictures uploaded this frame are there for the shader
                mine.isf->renderRhi(cb, rc, tex, dst, cur);
                theirs.adoptClock(*mine.isf);
                if (!dst.ready()) {
                    r.final = nullptr;
                    return;
                }
                // Through its mask: the effect where the mask is white, the input where it is black; on the channels it
                // changes only
                const Layer *mask = theirs.maskLayer ? e.layer(e.indexOfId(theirs.maskLayer)) : nullptr;
                QRhiTexture *maskTex = nullptr;
                if (mask == &l) {
                    if (theirs.maskStage >= 0 && size_t(theirs.maskStage) <= k) maskTex = r.stage[size_t(theirs.maskStage)];
                } else if (mask) {
                    const auto found = layers.find(mask->id);
                    if (found != layers.end()) {
                        const RLayer &mr = found->second;
                        const int s = theirs.maskStage;
                        maskTex = s == 0 && mr.preFx ? mr.preFx
                                : s > 0 && size_t(s) < mr.stage.size() && mask->stagesWanted.count(s) && mr.stage[size_t(s)]
                                    ? mr.stage[size_t(s)]
                                    : mr.final;
                    }
                }
                const bool masked = mask && maskTex;
                const bool someChannels = !std::all_of(theirs.channels.begin(), theirs.channels.end(), [](bool on) { return on; });
                if (masked || someChannels || theirs.mix < 1) {
                    RhiTarget &mixed = r.mixed[ping];
                    mixed.ensure(rhi.get(), cur);
                    const float u[8] = {theirs.channels[0] ? 1.f : 0.f, theirs.channels[1] ? 1.f : 0.f, theirs.channels[2] ? 1.f : 0.f,
                                        theirs.channels[3] ? 1.f : 0.f, float(std::clamp(theirs.mix, 0.0, 1.0)), masked ? 1.f : 0.f,
                                        theirs.maskInvert ? 1.f : 0.f, 0};
                    if (!mixed.ready() || !fullscreen(Draw::EffectMask, mixed, u, sizeof u, {tex, dst.texture(), masked ? maskTex : dst.texture()})) {
                        r.final = nullptr;
                        return;
                    }
                    tex = mixed.texture();
                } else {
                    tex = dst.texture();
                }
                // A stage a mask takes: kept as it is now
                if (l.stagesWanted.count(int(k) + 1)) {
                    RhiTarget &kept = r.stageTargets[int(k) + 1];
                    kept.ensure(rhi.get(), cur);
                    if (kept.ready() && res().blit(cb, tex, kept, take())) r.stage[k + 1] = kept.texture();
                } else {
                    r.stage[k + 1] = tex;
                }
                if (theirs.maskOnly) tex = input;
                else ping = 1 - ping;
            }
        }
        tex = applySurfaceMasks(l, r, tex);
        if (tex) tex = applyPictureMatte(l, r, tex);
        r.final = tex; // none: a mask or a matte that could not be drawn never lets the picture through
    }

    // A Text layer's picture: redrawn only when what is shown changes (typed characters, edits, style), as before
    QRhiTexture *textPicture(Layer &l, RLayer &r)
    {
        // A Timecode generator: its clock; a Countdown, Stopwatch or Clock: its time (an overtime in a color of its own)
        const QString shown = l.tcClock ? l.tcClock->display() : l.clockGen ? l.clockGen->display() : l.text.shown();
        const QColor fill = l.clockGen ? l.clockGen->color(l.text.color) : l.text.color;
        const QString key = QStringLiteral("%1\n%2|%3|%4|%5|%6|%7|%8x%9|%10").arg(shown, l.text.font).arg(l.text.size)
                                .arg(fill.name(QColor::HexArgb)).arg(int(l.text.align))
                                .arg(l.text.lineHeight).arg(l.text.letterSpacing).arg(l.text.width).arg(l.text.height)
                                .arg(QStringLiteral("%1%2%3%4|%5%6|%7%8|%9").arg(l.text.bold).arg(l.text.italic).arg(l.text.underline)
                                         .arg(l.text.strike).arg(l.text.outline).arg(l.text.outlineColor.name(QColor::HexArgb))
                                         .arg(l.text.shadow).arg(l.text.shadowColor.name(QColor::HexArgb))
                                         .arg(QStringLiteral("%1,%2").arg(l.text.shadowX).arg(l.text.shadowY)));
        const float written = l.tcClock || l.clockGen ? 1.0f : l.text.writeProgress;
        QImage img;
        if (written < 1.0f || l.text.writePlaying) { // being written: the picture revealed along the way of the pen
            if (!l.textWriting || key != l.textWritingKey) {
                QFont font(l.text.font);
                font.setPixelSize(std::max(1, l.text.size));
                l.textWriting = TextWriting::build(renderTextLayer(l, shown, fill, true), renderTextLayer(l, shown, fill), true,
                                                   QFontMetricsF(font).lineSpacing() * std::max(0.1f, l.text.lineHeight));
                l.textWritingKey = key;
            }
            l.textWriting->setProgress(written);
            img = l.textWriting->image();
            r.textKey.clear(); // written: the whole picture again
        } else if (!r.text || key != r.textKey) {
            l.textWriting.reset();
            img = renderTextLayer(l, shown, fill);
            r.textKey = key;
        }
        if (!img.isNull()) {
            const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888); // first row: the bottom
            if (!r.text || r.text->pixelSize() != rgba.size()) {
                r.text.reset(rhi->newTexture(QRhiTexture::RGBA8, rgba.size()));
                if (!r.text->create()) r.text.reset();
            }
            if (r.text) batch()->uploadTexture(r.text.get(), rgba);
        }
        return r.text.get();
    }

    // The layer a transition begins on keeps its place and id, and its old source goes on as the transition's `from`:
    // what this renderer kept for the layer (its decoder's texture, its image, its effects) goes with it
    void adoptOutgoing(quint64 id)
    {
        const auto t = e.m_transitions.find(id);
        if (t == e.m_transitions.end()) return;
        const Layer *from = t->second->from.get();
        if (outgoing.count(from)) return;
        const auto mine = layers.find(id);
        RLayer &o = outgoing[from];
        if (mine == layers.end()) return;
        o = std::move(mine->second);
        layers.erase(mine);
        o.used = true;
    }

    // The outgoing source's picture (its ROI, color and effects) and the incoming one, mixed by the transition into the
    // layer's picture; the layer's mapping, opacity and blend then apply to the mix (Render.cpp's renderTransition)
    void renderTransition(Layer &l, RLayer &r, Engine::SourceTransition &t, const IsfRenderContext &rc)
    {
        RLayer &o = outgoing[t.from.get()];
        o.used = true;
        renderLayer(*t.from, o, rc);
        QRhiTexture *a = o.final, *b = r.final;
        const QSize size = b ? b->pixelSize() : a ? a->pixelSize() : QSize();
        if (size.isEmpty()) return;
        if (!r.transition || r.transitionPath != t.shaderPath) {
            r.transition.reset();
            r.transitionPath = t.shaderPath;
            if (!t.shaderPath.isEmpty()) {
                auto s = std::make_unique<IsfInstance>();
                if (s->loadRhi(t.shaderPath, rhi.get()) && s->input(QStringLiteral("startImage")) && s->input(QStringLiteral("endImage")))
                    r.transition = std::move(s);
                else qWarning("QRhi renderer: transition %s: %s", qPrintable(t.shaderPath), qPrintable(s->error().left(200)));
            }
        }
        const double p = std::clamp(t.elapsed / t.duration, 0.0, 1.0);
        const double eased = p * p * (3 - 2 * p);
        r.transitionTarget.ensure(rhi.get(), size);
        if (!r.transitionTarget.ready()) return; // keep the last valid incoming layer output
        if (r.transition) {
            auto image = [&](const char *name, QRhiTexture *tex) {
                IsfInput *in = r.transition->input(QString::fromLatin1(name));
                in->rhiExt = tex;
                in->extW = tex ? tex->pixelSize().width() : 0;
                in->extH = tex ? tex->pixelSize().height() : 0;
            };
            image("startImage", a);
            image("endImage", b);
            if (IsfInput *in = r.transition->input(QStringLiteral("progress"))) in->fValue = eased;
            r.transition->renderRhi(cb, rc, nullptr, r.transitionTarget, size);
        } else { // a crossfade
            const float u[4] = {float(eased), 0, 0, 0};
            if (!fullscreen(Draw::Fade, r.transitionTarget, u, sizeof u, {a, b})) return;
        }
        r.incoming = b;
        r.final = r.transitionTarget.texture();
    }

    // A mask's alpha as a texture of its own, drawn again when the layer's masks or the frame's size change
    QRhiTexture *maskTexture(std::unique_ptr<QRhiTexture> &tex, unsigned &rev, QSize &made, const Layer &l, Mask::Attach attach, QSize size)
    {
        if (!tex || rev != l.masksRevision || made != size) {
            const QImage img = masks::raster(l.masks, attach, size).convertToFormat(QImage::Format_RGBA8888);
            if (img.isNull()) return nullptr;
            if (!tex || tex->pixelSize() != img.size()) {
                tex.reset(rhi->newTexture(QRhiTexture::RGBA8, img.size()));
                if (!tex->create()) {
                    tex.reset();
                    return nullptr;
                }
            }
            batch()->uploadTexture(tex.get(), img);
            rev = l.masksRevision;
            made = size;
        }
        return tex.get();
    }

    // Its surface masks: the picture through their alpha (a viewport's color too, as its screen shows it opaque)
    QRhiTexture *applySurfaceMasks(Layer &l, RLayer &r, QRhiTexture *tex)
    {
        if (!tex || !masks::any(l.masks, Mask::Attach::Surface)) {
            r.shapeMasked.release();
            r.surfaceMask.reset();
            return tex;
        }
        const QSize size = tex->pixelSize();
        QRhiTexture *mask = maskTexture(r.surfaceMask, r.surfaceMaskRev, r.surfaceMaskSize, l, Mask::Attach::Surface, maskRasterSize(size));
        r.shapeMasked.ensure(rhi.get(), size);
        const float u[4] = {l.isViewport ? 1.f : 0.f, 0, 0, 0};
        if (!mask || !r.shapeMasked.ready() || !fullscreen(Draw::ShapeMask, r.shapeMasked, u, sizeof u, {tex, mask})) return nullptr;
        return r.shapeMasked.texture();
    }

    // A matte in Picture space: the matte layer's picture stretched over this one's (after its effects and its surface
    // masks). A matte layer with no picture: nothing shows (or all, inverted).
    QRhiTexture *applyPictureMatte(Layer &l, RLayer &r, QRhiTexture *tex)
    {
        if (!l.matte.layer || l.matte.space != LayerMatte::Space::Picture) {
            r.mattePicture.release();
            return tex;
        }
        const Layer *m = e.layer(e.indexOfId(l.matte.layer));
        QRhiTexture *matte = nullptr;
        if (m && m != &l) {
            const auto found = layers.find(m->id);
            if (found != layers.end()) matte = found->second.final;
        }
        r.mattePicture.ensure(rhi.get(), tex->pixelSize());
        const float u[4] = {float(Engine::matteMode(l.matte)), 0, 0, 0};
        if (!r.mattePicture.ready() || !fullscreen(Draw::PictureMatte, r.mattePicture, u, sizeof u, {tex, matte})) return nullptr;
        return r.mattePicture.texture();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Compositing
    // ---------------------------------------------------------------------------------------------------------------
    // A layer's mapped mesh in a vertex buffer of its own, made again when its mapping changes (Render.cpp's bindMesh)
    bool mesh(Layer &l, RLayer &r)
    {
        const Mapping &m = e.effectiveMapping(l), &was = r.meshShape;
        bool same = r.meshMade && was.cols == m.cols && was.rows == m.rows && was.offsets == m.offsets && was.position == m.position
                    && was.size == m.size && was.rotation == m.rotation && was.pivot == m.pivot && was.aspect == m.aspect;
        for (int i = 0; same && i < 4; ++i) same = was.pins[i] == m.pins[i];
        if (!same) {
            r.meshGrid = std::any_of(std::begin(m.pins), std::end(m.pins), [](const QPointF &p) { return !p.isNull(); })
                         || std::any_of(m.offsets.begin(), m.offsets.end(), [](const QPointF &p) { return !p.isNull(); });
            std::vector<float> vertices;
            m.buildVertices(r.meshGrid ? kMeshSubdiv : 1, vertices);
            const quint32 bytes = quint32(vertices.size() * sizeof(float));
            if (!r.mesh || r.mesh->size() != bytes) {
                r.mesh.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, bytes));
                if (!r.mesh->create()) return false;
            }
            batch()->uploadStaticBuffer(r.mesh.get(), vertices.data());
            r.meshShape = m;
            r.meshMade = true;
        }
        return true;
    }

    struct Item {
        QRhiShaderResourceBindings *srb;
        QRhiGraphicsPipeline *ps;
        QRhiBuffer *mesh;
        bool grid;
    };

    // The uniforms every layer of a composite shares: the part of the composition the target shows (`view`, normalized,
    // origin top left) turned by `angle`
    void viewUniforms(float u[16], const QRectF &view, double angle)
    {
        const double rw = std::max(1e-6, view.width()), rh = std::max(1e-6, view.height());
        const double ang = angle * 3.14159265358979323846 / 180;
        const QSize comp = e.m_compSize;
        const float asp = float(double(comp.width()) / std::max(1, comp.height()));
        const float v[16] = {float(1.0 / rw), float(1.0 / rh), float((1.0 - 2.0 * view.left()) / rw - 1.0),
                             float(1.0 - (1.0 - 2.0 * view.top()) / rh), float(std::cos(ang)), float(std::sin(ang)),
                             float(view.center().x() * 2.0 - 1.0), float(1.0 - view.center().y() * 2.0), asp,
                             res().ndcFlip(), 1, 0, 0, 0, 0, 0};
        std::copy(v, v + 16, u);
    }

    // One layer's draw: its picture at `opacity` through its composition masks and, when `withMatte`, its matte in
    // Composition space (prepareMatte's). `dst`: what a Difference reads. False: it cannot be drawn.
    bool makeItem(Layer &l, RLayer &r, const float *shared, float opacity, BlendMode blend, bool withMatte, const RhiTarget &target,
                  QRhiTexture *dst, Item &out, bool ms = false, QRhiTexture *picture = nullptr, QRhiSampler *sampler = nullptr)
    {
        if (!mesh(l, r)) return false;
        float u[16];
        std::copy(shared, shared + 16, u);
        u[10] = opacity;
        QRhiTexture *mask = nullptr;
        if (!l.isViewport && masks::any(l.masks, Mask::Attach::Composition)) {
            mask = maskTexture(r.compositionMask, r.compositionMaskRev, r.compositionMaskSize, l, Mask::Attach::Composition,
                               maskRasterSize(e.m_compSize));
            if (!mask) return false; // a mask that cannot be drawn never lets the picture through
            u[11] = 1;
        }
        QRhiTexture *matte = nullptr;
        if (withMatte && l.matte.layer && l.matte.space == LayerMatte::Space::Composition && r.matteTarget.ready()) {
            matte = r.matteTarget.texture();
            u[12] = float(Engine::matteMode(l.matte));
        }
        const auto all = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
        const auto fs = QRhiShaderResourceBinding::FragmentStage;
        std::vector<RhiResources::Bind> b{{0, all, ubuf(u, sizeof u)},
                                          {1, fs, nullptr, picture ? picture : r.final, sampler ? sampler : res().linear()},
                                          {2, fs, nullptr, mask ? mask : res().black(), res().linear()},
                                          {3, fs, nullptr, matte ? matte : res().black(), res().linear()}};
        if (dst) b.push_back({4, fs, nullptr, dst, res().nearest()});
        QRhiShaderResourceBindings *srb = res().bindings(b);
        QRhiGraphicsPipeline *ps = srb ? pipeline(dst ? Draw::CompositeDiff : Draw::Composite, blend, target, srb, ms) : nullptr;
        if (!ps) return false;
        out = {srb, ps, r.mesh.get(), r.meshGrid};
        return true;
    }

    // The items drawn in order, inside a pass
    void drawItems(const std::vector<Item> &items, const RhiTarget &target)
    {
        cb->setViewport({0, 0, float(target.width()), float(target.height())});
        for (const Item &it : items) {
            cb->setGraphicsPipeline(it.ps);
            cb->setViewport({0, 0, float(target.width()), float(target.height())}); // after the pipeline: Vulkan then sets the scissor to it
            cb->setShaderResources(it.srb);
            const QRhiCommandBuffer::VertexInput vin(it.mesh, 0);
            cb->setVertexInput(0, 1, &vin, it.grid ? gridIndices.get() : quadIndices.get(), 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(quint32(it.grid ? gridIndexCount : 6));
        }
    }

    // A layer's picture with its mip levels, made again when the picture can have changed (once per frame of a plain
    // video, once per frame of the render for any other): sampled from them, a picture drawn smaller than it is neither
    // shimmers nor moirés. Nothing: drawn plainly.
    QRhiTexture *mipPicture(Layer &l, RLayer &r)
    {
        QRhiTexture *src = r.final;
        if (!src) return nullptr;
        const bool plainVideo = l.type == SourceType::Video && r.video && src == r.video->texture();
        const quint64 serial = plainVideo ? r.videoSerial : e.m_frameCount.load(std::memory_order_relaxed);
        const int kind = plainVideo ? 1 : 2;
        if (r.mip.ensureMipped(rhi.get(), src->pixelSize(), src->format())) r.mipSource = nullptr;
        if (!r.mip.readyMipped(src->pixelSize(), src->format())) return nullptr;
        if (r.mipSource != src || r.mipSerial != serial || r.mipKind != kind) {
            if (!res().blit(cb, src, r.mip)) return nullptr;
            QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
            u->generateMips(r.mip.texture());
            cb->resourceUpdate(u);
            ++e.m_mipmapsMade;
            r.mipSource = src;
            r.mipSerial = serial;
            r.mipKind = kind;
        }
        return r.mip.texture();
    }

    // Draws the layers into the target; `view`: the part of the composition the target shows (normalized, origin top
    // left). The first layer is on top. `level` < 1: the target dimmed after (composition opacity, blackout).
    // A layer by Difference reads a copy of what is drawn before it: the passes before it end, the copy is made, and
    // the layer is drawn in a pass of its own. Antialiasing: drawn into a multisampled buffer, resolved at each pass's end.
    void composite(RhiTarget &target, const std::vector<Layer *> &list, const QRectF &view, QColor clear, quint64 viewport = 0,
                   double angle = 0, double level = 1)
    {
        if (!target.ready()) return;
        const Engine::RenderSettings rs = e.effectiveRender();
        const bool ms = target.ensureMsaa(rs.samples) > 1;
        float shared[16];
        viewUniforms(shared, view, angle);
        std::vector<Item> run;
        bool begun = false;
        // A copy of the target (what is drawn so far, resolved), for a Difference layer to read and for a multisampled
        // pass to start from
        auto copyOfTarget = [&]() -> QRhiTexture * {
            RhiTarget &copy = dstCopies[{target.width(), target.height(), int(target.format())}];
            copy.ensure(rhi.get(), target.size(), target.format());
            return copy.ready() && res().blit(cb, target.texture(), copy) ? copy.texture() : nullptr;
        };
        // The pass for what is in `run`. A multisampled buffer is not kept from one pass to the next, so a later pass
        // starts by drawing what the earlier ones resolved (`before`: a copy of it, if one is made)
        auto flush = [&](QRhiTexture *before = nullptr) {
            if (begun && run.empty()) return;
            if (ms && begun && !before) before = copyOfTarget();
            cb->beginPass(ms ? target.msCleared() : begun ? target.kept() : target.cleared(), clear, {1.0f, 0}, take());
            if (ms && begun && before) {
                std::vector<RhiResources::Bind> b{{15, QRhiShaderResourceBinding::VertexStage, flip.get()},
                                                  {1, QRhiShaderResourceBinding::FragmentStage, nullptr, before, res().linear()}};
                QRhiShaderResourceBindings *srb = res().bindings(b);
                QRhiGraphicsPipeline *ps = srb ? pipeline(Draw::Restore, BlendMode::Normal, target, srb, true) : nullptr;
                if (ps) {
                    cb->setGraphicsPipeline(ps);
                    cb->setViewport({0, 0, float(target.width()), float(target.height())});
                    cb->setShaderResources(srb);
                    cb->draw(3);
                }
            }
            drawItems(run, target);
            cb->endPass();
            run.clear();
            begun = true;
        };
        for (int i = int(list.size()) - 1; i >= 0; --i) {
            Layer &l = *list[size_t(i)];
            const float opacity = viewport ? l.opacity * l.opacityIn(viewport) : l.opacity;
            auto found = layers.find(l.id);
            if (l.isMappingZone || !l.enabled || l.soloHidden || opacity <= 0.0f || found == layers.end() || !found->second.final) continue;
            RLayer &r = found->second;
            BlendMode blend = l.blend;
            QRhiTexture *dst = nullptr;
            if (blend == BlendMode::Difference) {
                flush(); // what is drawn so far, copied
                dst = copyOfTarget();
                if (!dst) blend = BlendMode::Normal; // degraded but safe: ordinary source-over blending
            }
            // Drawn smaller than it is: sampled from its mip levels
            QRhiTexture *picture = nullptr;
            if (rs.mipmaps > 0) {
                const QSize pixels = r.final->pixelSize();
                const QRectF b = e.effectiveMapping(l).bounds();
                const double drawnW = b.width() / std::max(1e-6, view.width()) * target.width();
                const double drawnH = b.height() / std::max(1e-6, view.height()) * target.height();
                if (drawnW < pixels.width() * 0.9 || drawnH < pixels.height() * 0.9) picture = mipPicture(l, r);
            }
            Item it;
            if (makeItem(l, r, shared, opacity, blend, true, target, dst, it, ms, picture, picture ? res().trilinear() : nullptr)) run.push_back(it);
            if (dst) flush(dst); // alone: it starts from the copy it read
        }
        flush();
        dim(target, level);
    }

    // A matte in Composition space: the matte layer drawn alone, where it is in the composition (its mapping, its
    // opacity, its own composition masks), on a transparent canvas the size of the composition. Drawn even when that
    // layer is switched off (a matte is usually hidden). A matte layer with no picture: nothing shows (or all, inverted).
    void prepareMatte(Layer &l, RLayer &r)
    {
        if (!l.matte.layer || l.matte.space != LayerMatte::Space::Composition) {
            r.matteTarget.release();
            return;
        }
        if (!l.needed || !r.final || l.opacity <= 0.0f) return; // only used to composite a visible picture
        r.matteTarget.ensure(rhi.get(), e.m_compSize);
        if (!r.matteTarget.ready()) return; // drawn without it rather than from a stale one
        std::vector<Item> run;
        Layer *m = e.layer(e.indexOfId(l.matte.layer));
        const auto found = m ? layers.find(m->id) : layers.end();
        if (found != layers.end() && found->second.final) {
            float shared[16];
            viewUniforms(shared, QRectF(0, 0, 1, 1), 0);
            Item it;
            if (makeItem(*m, found->second, shared, m->opacity, BlendMode::Normal, false, r.matteTarget, nullptr, it)) run.push_back(it);
        }
        cb->beginPass(r.matteTarget.cleared(), Qt::transparent, {1.0f, 0}, take());
        drawItems(run, r.matteTarget);
        cb->endPass();
    }

    void renderGroup(Layer &g, RLayer &r, const std::vector<Layer *> &members, const IsfRenderContext &rc)
    {
        r.final = r.raw = nullptr;
        r.group.ensure(rhi.get(), e.m_compSize);
        if (!r.group.ready()) return;
        composite(r.group, members, QRectF(0, 0, 1, 1), Qt::transparent);
        processLayer(g, r, r.group.texture(), r.group.size(), true, rc);
    }

    // A layer's picture at a stage, as the masks take it: -1 final, 0 its source, k after its FX k (when it is kept)
    QRhiTexture *stageTexture(quint64 id, int stage)
    {
        const auto found = id ? layers.find(id) : layers.end();
        if (found == layers.end()) return nullptr;
        const RLayer &r = found->second;
        const Layer *l = e.layer(e.indexOfId(id));
        if (stage == 0 && r.preFx) return r.preFx;
        if (stage > 0 && l && size_t(stage) < r.stage.size() && l->stagesWanted.count(stage) && r.stage[size_t(stage)]) return r.stage[size_t(stage)];
        if (stage == Engine::kStageHdrFile) return r.hdr ? r.hdr.get() : r.final; // the HDR file as read
        return r.final;
    }
    // The pictures that models carry (laid on them, or pushing their points) as textures of this QRhi
    void resolveScenePictures(std::vector<Scene3D::Item> &items)
    {
        for (Scene3D::Item &item : items) {
            item.pictureRhi = stageTexture(item.pictureLayer, item.pictureStage);
            item.displaceRhi = stageTexture(item.displaceLayer, item.displaceStage);
        }
    }

    // A 3D scene's picture (Render.cpp's renderScene), the size of the composition, then its layer's own processing
    void renderScene(Layer &g, RLayer &r, const std::vector<Layer *> &members, const IsfRenderContext &rc)
    {
        r.final = r.raw = r.preFx = nullptr;
        if (!r.scene) r.scene = std::make_unique<RhiScene>(rhi.get());
        std::vector<Scene3D::Item> items;
        std::vector<Scene3D::Lamp> lamps;
        e.collectScene(members, items, lamps);
        resolveScenePictures(items);
        const Layer *environment = g.sceneEnvironment ? e.layer(e.indexOfId(g.sceneEnvironment)) : nullptr;
        QRhiTexture *envTexture = environment ? stageTexture(environment->id, g.sceneEnvironmentStage) : nullptr;
        const bool linear = environment && g.sceneEnvironmentStage == Engine::kStageHdrFile && layers.count(environment->id) && layers[environment->id].hdr;
        QRhiTexture *picture = r.scene->draw(cb, take(), e.placedCamera(g), items, lamps, e.m_compSize.width(), e.m_compSize.height(), e.m_quality3d, envTexture, linear);
        g.sceneComputed = r.scene->triangles();
        if (picture) processLayer(g, r, picture, picture->pixelSize(), true, rc);
    }

    // A viewport's output (Render.cpp's renderViewport, then its part of composite())
    void renderViewport(Layer &v, RLayer &r, const std::vector<Layer *> &shown, const IsfRenderContext &rc, double level)
    {
        r.final = r.raw = r.preFx = nullptr;
        if (!v.enabled) return;
        const QSize size = v.viewportSize();
        if (v.testCard) { // what goes to its screen, as it is (no ROI, color or effect)
            const QString output = v.vpResolvedScreen.isEmpty() ? v.vpScreen : v.vpResolvedScreen;
            const QString key = QStringLiteral("%1|%2|%3x%4").arg(v.name, output).arg(size.width()).arg(size.height());
            if (!r.card || r.cardKey != key) {
                const QImage still = TestCard::still(v.name, size, output);
                r.card.reset(rhi->newTexture(QRhiTexture::RGBA8, still.size()));
                if (!r.card->create()) r.card.reset();
                else batch()->uploadTexture(r.card.get(), still);
                r.cardKey = key;
            }
            r.out.ensure(rhi.get(), size);
            if (!r.card || !r.out.ready()) return;
            const TestCard::Motion m = TestCard::motion(size);
            const float u[12] = {float(cardClock.elapsed() / 1000.0), m.size[0], m.size[1], 0, m.circle[0], m.circle[1], m.circle[2], 0,
                                 m.band[0], m.band[1], m.band[2], m.band[3]};
            if (!fullscreen(Draw::TestCard, r.out, u, sizeof u, {r.card.get()})) return;
            r.final = r.raw = r.preFx = r.out.texture();
            return;
        }
        const Mapping::Rect rr = v.mapping.rect(e.m_compSize);
        const double cw = std::max(1, e.m_compSize.width()), ch = std::max(1, e.m_compSize.height());
        QRectF view(0, 0, rr.w / cw, rr.h / ch);
        view.moveCenter(QPointF(rr.center.x() / cw, rr.center.y() / ch));
        r.out.ensure(rhi.get(), size);
        if (!r.out.ready()) return;
        if (e.viewportIsPlain(v, shown)) { // its layers straight into its output, over black
            composite(r.out, shown, view, QColor(0, 0, 0, 255), v.id, rr.angle, level);
            r.final = r.raw = r.preFx = r.out.texture();
            return;
        }
        r.group.ensure(rhi.get(), size);
        if (!r.group.ready()) return;
        composite(r.group, shown, view, Qt::transparent, v.id, rr.angle);
        processLayer(v, r, r.group.texture(), r.group.size(), true, rc);
        QRhiTexture *picture = r.final;
        r.final = r.out.texture(); // what shows it: its output
        if (!picture || v.opacity <= 0.0f) {
            cb->beginPass(r.out.cleared(), QColor(0, 0, 0, 255), {1.0f, 0}, take());
            cb->endPass();
            dim(r.out, level);
            return;
        }
        const QString &targetSpace = v.outputSpace == QLatin1String("same") ? v.outputInputSpace : v.outputSpace;
        if (targetSpace != v.outputInputSpace || v.outputExposure != 0) {
            // Black-matted, then decoded, exposed and encoded once
            const float u[8] = {v.opacity, float(std::exp2(v.outputExposure)), float(outputColorSpaceIndex(v.outputInputSpace)),
                                float(outputColorSpaceIndex(targetSpace)), 0, 0, 0, 0};
            fullscreen(Draw::OutputColor, r.out, u, sizeof u, {picture}, QColor(0, 0, 0, 255));
        } else {
            // Its picture fills its output at its opacity (the whole frame: a mapping of its own size)
            const float u[16] = {1, 1, 0, 0, 1, 0, 0, 0, 1, res().ndcFlip(), v.opacity, 0, 0, 0, 0, 0};
            const auto all = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
            const auto fs = QRhiShaderResourceBinding::FragmentStage;
            QRhiShaderResourceBindings *srb = res().bindings({{0, all, ubuf(u, sizeof u)},
                                                              {1, fs, nullptr, picture, res().linear()},
                                                              {2, fs, nullptr, res().black(), res().linear()},
                                                              {3, fs, nullptr, res().black(), res().linear()}});
            QRhiGraphicsPipeline *ps = srb ? pipeline(Draw::Composite, BlendMode::Normal, r.out, srb) : nullptr;
            cb->beginPass(r.out.cleared(), QColor(0, 0, 0, 255), {1.0f, 0}, take());
            if (ps) {
                cb->setGraphicsPipeline(ps);
                cb->setViewport({0, 0, float(r.out.width()), float(r.out.height())});
                cb->setShaderResources(srb);
                const QRhiCommandBuffer::VertexInput vin(fullMesh.get(), 0);
                cb->setVertexInput(0, 1, &vin, quadIndices.get(), 0, QRhiCommandBuffer::IndexUInt32);
                cb->drawIndexed(6);
            }
            cb->endPass();
        }
        dim(r.out, level);
    }

    // The viewport's picture into its window: a triangle over the swapchain's backbuffer, the picture sampled as it is
    bool present(quint64 viewport, QWindow *window, bool waitForDisplay)
    {
        const auto found = layers.find(viewport);
        if (!window || found == layers.end() || !found->second.final || !presentFlip) return false;
        QRhiTexture *picture = found->second.final;
        Output &o = outputs[viewport];
        if (o.window != window || !o.sc) {
            o = Output();
            o.window = window;
            o.sc.reset(rhi->newSwapChain());
            o.sc->setWindow(window);
            o.rp.reset(o.sc->newCompatibleRenderPassDescriptor());
            o.sc->setRenderPassDescriptor(o.rp.get());
        }
        QRhiSwapChain::Flags flags;
        if (!waitForDisplay) flags |= QRhiSwapChain::NoVSync;
        if (capturePresented) flags |= QRhiSwapChain::UsedAsTransferSource;
        if (o.sc->flags() != flags) {
            o.sc->setFlags(flags);
            o.resize = true;
        }
        if (o.resize || o.sc->currentPixelSize() != o.sc->surfacePixelSize()) {
            if (!o.sc->createOrResize()) return false;
            o.resize = false;
        }
        QRhi::FrameOpResult status = rhi->beginFrame(o.sc.get());
        if (status == QRhi::FrameOpSwapChainOutOfDate) {
            if (!o.sc->createOrResize()) return false;
            status = rhi->beginFrame(o.sc.get());
        }
        if (status != QRhi::FrameOpSuccess) return false;
        QRhiCommandBuffer *pcb = o.sc->currentFrameCommandBuffer();
        QRhiShaderResourceBindings *srb = res().bindings({{15, QRhiShaderResourceBinding::VertexStage, presentFlip.get()},
                                                          {1, QRhiShaderResourceBinding::FragmentStage, nullptr, picture, res().linear()}});
        if (!o.ps && srb) {
            o.ps.reset(rhi->newGraphicsPipeline());
            o.ps->setShaderStages({{QRhiShaderStage::Vertex, RhiResources::shader(QStringLiteral("fullscreen.vert"))},
                                   {QRhiShaderStage::Fragment, RhiResources::shader(QStringLiteral("blit.frag"))}});
            o.ps->setShaderResourceBindings(srb);
            o.ps->setRenderPassDescriptor(o.rp.get());
            if (!o.ps->create()) o.ps.reset();
        }
        pcb->beginPass(o.sc->currentFrameRenderTarget(), QColor(0, 0, 0, 255), {1.0f, 0});
        if (o.ps && srb) {
            pcb->setGraphicsPipeline(o.ps.get());
            const QSize px = o.sc->currentPixelSize();
            pcb->setViewport({0, 0, float(px.width()), float(px.height())});
            pcb->setShaderResources(srb);
            pcb->draw(3);
        }
        pcb->endPass();
        if (capturePresented) {
            auto *result = new QRhiReadbackResult;
            result->completed = [this, viewport, result] {
                const auto it = outputs.find(viewport);
                if (it != outputs.end()) {
                    const QImage::Format format = result->format == QRhiTexture::BGRA8 ? QImage::Format_ARGB32 : QImage::Format_RGBA8888;
                    it->second.last = QImage(reinterpret_cast<const uchar *>(result->data.constData()), result->pixelSize.width(),
                                             result->pixelSize.height(), format).convertToFormat(QImage::Format_RGBA8888);
                }
                delete result;
            };
            QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
            u->readBackTexture(QRhiReadbackDescription(), result);
            pcb->resourceUpdate(u);
        }
        rhi->endFrame(o.sc.get());
        return !o.ps ? false : true;
    }

    // Every layer in dependency order (Render.cpp's renderPass)
    // `only`: just these layers (by id) are drawn, the others hold the picture they have (the pause)
    void renderPass(const IsfRenderContext &rc, double level, const std::set<quint64> *only = nullptr)
    {
        auto &all = e.m_layers;
        const size_t n = all.size();
        enum { Todo = 0, Doing = 1, Done = 2 };
        marks.assign(n, Todo);
        std::function<void(size_t)> render = [&](size_t i) {
            if (marks[i] != Todo) return;
            if (only && !only->count(all[i]->id)) { // held: its picture stays as it is
                marks[i] = Done;
                return;
            }
            marks[i] = Doing;
            Layer &l = *all[i];
            if (!l.isGroup && !l.isViewport) adoptOutgoing(l.id);
            RLayer &r = layers[l.id];
            r.used = true;
            const auto dependency = [&](size_t row, int) { render(row); };
            if (l.needed && (!l.isViewport || (l.enabled && !l.testCard))) e.forEachRenderDependency(l, dependency);
            if (const auto t = l.isGroup || l.isViewport ? e.m_transitions.end() : e.m_transitions.find(l.id); t != e.m_transitions.end())
                e.forEachRenderDependency(*t->second->from, dependency);
            if (l.isViewport) {
                std::vector<Layer *> shown;
                for (size_t k = 0; k < n; ++k) {
                    Layer &o = *all[k];
                    if (o.isViewport || o.parent || !(o.opacityIn(l.id) > 0.0f)) continue;
                    render(k);
                    shown.push_back(&o);
                }
                renderViewport(l, r, shown, rc, level);
            } else if (l.isGroup) {
                const bool draw = l.needed && !l.merge3d;
                std::vector<Layer *> members;
                for (const size_t k : e.m_renderChildren[i]) {
                    render(k);
                    if (draw) members.push_back(all[k].get());
                }
                if (!draw) r.final = r.raw = r.preFx = nullptr;
                else if (l.scene3d) renderScene(l, r, members, rc);
                else renderGroup(l, r, members, rc);
            } else {
                renderLayer(l, r, rc);
                if (const auto t = e.m_transitions.find(l.id); t != e.m_transitions.end()) renderTransition(l, r, *t->second, rc);
            }
            if (!l.isViewport) prepareMatte(l, r); // before it is composited (a viewport or a group draws it later)
            if (l.needed) l.pictureCulled = false;
            l.finalW = r.final ? r.final->pixelSize().width() : 0; // the size of its picture, after its ROI (the mask editor's frame)
            l.finalH = r.final ? r.final->pixelSize().height() : 0;
            marks[i] = Done;
        };
        for (size_t i = 0; i < n; ++i) render(i);
    }
};

RhiRenderer::RhiRenderer(Engine &engine) : d(std::make_unique<Impl>(engine)) {}

RhiRenderer::~RhiRenderer()
{
    if (!d->rhi) return;
    if (d->pending) d->pending->release();
    if (!d->rhi->isDeviceLost()) d->rhi->finish();
    d->outputs.clear(); // swapchains before the QRhi
    d->pipelines.clear();
    RhiResources::of(d->rhi.get()).resetPipelines();
    d->layers.clear();
    d->outgoing.clear();
    d->compositionOut.release();
    d->ubufs.clear();
    d->flip.reset();
    d->gridIndices.reset();
    d->quadIndices.reset();
    d->fullMesh.reset();
    d->presentFlip.reset();
    RhiResources::release(d->rhi.get());
    d->rhi.reset();
}

QRhi *RhiRenderer::rhi() const { return d->rhi.get(); }

bool RhiRenderer::init(QString *err)
{
    QString backend;
    d->rhi = rhidevice::create({}, &backend);
    if (!d->rhi) {
        if (err) *err = QStringLiteral("No QRhi backend (Metal, Direct3D 11, Vulkan) on this machine");
        return false;
    }
    qInfo().noquote() << "QRhi renderer:" << backend << d->rhi->driverInfo().deviceName;
    QRhi *rhi = d->rhi.get();
    {   // What the device is, and what the decoders may leave to it
        const QRhiDriverInfo info = rhi->driverInfo();
        const char *vendor = info.vendorId == 0x10DE ? "NVIDIA" : info.vendorId == 0x1002 ? "AMD" : info.vendorId == 0x8086 ? "Intel"
                             : info.vendorId == 0x106B ? "Apple" : "";
        d->e.m_gpuRenderer = QString::fromUtf8(info.deviceName);
        d->e.m_gpuApi = backend;
        d->e.m_gpuVendor = *vendor ? QString::fromLatin1(vendor) : QStringLiteral("0x%1").arg(info.vendorId, 0, 16);
        d->e.m_gpuDxt = rhi->isTextureFormatSupported(QRhiTexture::BC1) && rhi->isTextureFormatSupported(QRhiTexture::BC3);
        d->e.m_gpuBptc = rhi->isTextureFormatSupported(QRhiTexture::BC7) && rhi->isTextureFormatSupported(QRhiTexture::BC6H);
        VideoDecoder::setGpuFormats(d->e.m_gpuDxt, d->e.m_gpuBptc);
        // Hardware-decoded frames sampled where they were decoded (macOS: VideoToolbox's surfaces as Metal textures)
        VideoDecoder::setZeroCopy(rhi->backend() == QRhi::Metal);
        qInfo().noquote() << "Compressed textures: DXT" << (d->e.m_gpuDxt ? "yes" : "no") << "- BPTC"
                          << (d->e.m_gpuBptc ? "yes" : "no (Hap R and Hap HDR decoded on the CPU)");
    }
    QRhiResourceUpdateBatch *u = d->batch();
    d->flip.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
    d->flip->create();
    const float flip[4] = {rhidevice::ndcFlip(rhi), 0, 0, 0};
    u->updateDynamicBuffer(d->flip.get(), 0, 16, flip);
    // The mesh's indices: a grid of kMeshSubdiv × kMeshSubdiv cells, or one cell (Engine::initialize's)
    auto indices = [&](int n, std::unique_ptr<QRhiBuffer> &buffer) {
        std::vector<quint32> idx;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const quint32 a = quint32(y * (n + 1) + x), b = a + 1, c = a + quint32(n + 1), dd = c + 1;
                idx.insert(idx.end(), {a, b, c, b, dd, c});
            }
        buffer.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, quint32(idx.size() * sizeof(quint32))));
        buffer->create();
        u->uploadStaticBuffer(buffer.get(), idx.data());
        return int(idx.size());
    };
    d->gridIndexCount = indices(kMeshSubdiv, d->gridIndices);
    indices(1, d->quadIndices);
    d->presentFlip.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
    d->presentFlip->create();
    const float reversed[4] = {-rhidevice::ndcFlip(rhi), 0, 0, 0};
    u->updateDynamicBuffer(d->presentFlip.get(), 0, 16, reversed);
    std::vector<float> full;
    Mapping().buildVertices(1, full);
    d->fullMesh.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, quint32(full.size() * sizeof(float))));
    d->fullMesh->create();
    u->uploadStaticBuffer(d->fullMesh.get(), full.data());
    return true;
}

void RhiRenderer::setSceneViews(std::vector<SceneViewRequest> views) { d->sceneViews = std::move(views); }

void RhiRenderer::hdrArrived(quint64 layer, const std::vector<float> &pixels, QSize size)
{
    if (!d->rhi || size.isEmpty() || pixels.size() < size_t(size.width()) * size_t(size.height()) * 4) return;
    Impl::RLayer &r = d->layers[layer];
    if (!r.hdr || r.hdr->pixelSize() != size) {
        r.hdr.reset(d->rhi->newTexture(QRhiTexture::RGBA32F, size));
        if (!r.hdr->create()) {
            r.hdr.reset();
            return;
        }
    }
    QRhiTextureSubresourceUploadDescription sub(pixels.data(), quint32(size.width()) * size.height() * 4 * sizeof(float));
    sub.setSourceSize(size);
    d->batch()->uploadTexture(r.hdr.get(), QRhiTextureUploadDescription({0, 0, sub}));
}

void RhiRenderer::imageArrived(quint64 layer, const QImage &image, const QImage &autoImage)
{
    if (!d->rhi || image.isNull()) return;
    d->adoptOutgoing(layer); // a transition just begun: the picture shown so far goes with the outgoing source
    Impl::RLayer &r = d->layers[layer];
    d->uploadPicture(r, image);
    if (autoImage.isNull()) r.autoImage.reset();
    else d->uploadPictureTo(r.autoImage, r.autoSize, autoImage);
}

void RhiRenderer::render(const IsfRenderContext &rc)
{
    if (!d->rhi || d->rhi->beginOffscreenFrame(&d->cb) != QRhi::FrameOpSuccess) return;
    d->nextUbuf = 0;
    if (d->dstCopies.size() > 8) d->dstCopies.clear();
    d->res().frameStarted();
    for (auto it = d->audioPictures.begin(); it != d->audioPictures.end();) { // not asked for two seconds: gone
        const quint64 now = d->e.m_frameCount.load(std::memory_order_relaxed) + 1;
        it = now - it->second.used > 120 ? d->audioPictures.erase(it) : std::next(it);
    }
    for (auto &[id, r] : d->layers) { // a transition over (stopped): the picture the layer shows is the incoming one, even held
        if (r.final && r.incoming && r.final == r.transitionTarget.texture() && !d->e.m_transitions.count(id)) r.final = r.incoming;
        r.used = false;
    }
    for (auto &[from, r] : d->outgoing) r.used = false;
    const double level = d->e.m_compositionOpacity.load() * d->e.m_blackLevel.load();
    if (!rc.paused) {
        d->renderPass(rc, level);
        for (auto &l : d->e.m_layers) l->renderPaused = false;
    } else {
        // Paused, nothing is drawn (feedback buffers and FRAMEINDEX hold too), except a picture loaded meanwhile: that layer,
        // the groups it is in and the viewports that show it are drawn once, the others keep their frame
        std::set<quint64> only;
        for (auto &lp : d->e.m_layers) {
            if (!lp->renderPaused) continue;
            lp->renderPaused = false;
            for (const Layer *x = lp.get(); x && only.insert(x->id).second; x = x->parent ? d->e.layer(d->e.indexOfId(x->parent)) : nullptr) {}
        }
        // The viewports are composited again from their layers' held pictures (the composition's fader and the blackout apply
        // to them as they move)
        for (auto &vp : d->e.m_layers)
            if (vp->isViewport) only.insert(vp->id);
        d->renderPass(rc, level, &only);
    }
    // The whole composition (the interface's preview)
    std::vector<Layer *> top;
    for (auto &l : d->e.m_layers)
        if (!l->parent && !l->isViewport) top.push_back(l.get());
    d->compositionOut.ensure(d->rhi.get(), d->e.m_compSize);
    d->composite(d->compositionOut, top, QRectF(0, 0, 1, 1), QColor(0, 0, 0, 255), 0, 0, level);
    // The previews the interface asked for: reduced, read back (the offscreen frame is done once it ends)
    struct Read {
        PreviewRequest request;
        QSize size;
        QRhiReadbackResult result;
    };
    std::vector<std::unique_ptr<Read>> reads;
    for (const PreviewRequest &q : d->previewRequests) {
        QRhiTexture *tex = nullptr;
        if (!q.id) tex = d->compositionOut.texture();
        else if (const auto found = d->layers.find(q.id); found != d->layers.end()) {
            const Layer *l = d->e.layer(d->e.indexOfId(q.id));
            if (q.tap == PreviewRequest::Raw) tex = found->second.raw;
            else if (q.tap == PreviewRequest::PreFx) tex = found->second.preFx;
            else tex = l && l->isViewport ? found->second.out.texture() : found->second.final;
        }
        if (!tex) continue;
        QSize size = tex->pixelSize();
        if (!q.exact.isEmpty()) size = q.exact;
        else if (size.width() > q.maxSide || size.height() > q.maxSide) size.scale(q.maxSide, q.maxSide, Qt::KeepAspectRatio);
        RhiTarget &small = d->previewTargets[q.slot];
        small.ensure(d->rhi.get(), size, QRhiTexture::RGBA8); // read back as 8-bit pixels
        if (!small.ready() || !d->res().blit(d->cb, tex, small, d->take())) continue;
        auto read = std::make_unique<Read>();
        read->request = q;
        read->size = small.size();
        QRhiResourceUpdateBatch *u = d->rhi->nextResourceUpdateBatch();
        u->readBackTexture({small.texture()}, &read->result);
        d->cb->resourceUpdate(u);
        reads.push_back(std::move(read));
    }
    // The 3D views: each drawn into a picture of its own, read back whole
    for (SceneViewRequest &q : d->sceneViews) {
        d->resolveScenePictures(q.items);
        std::unique_ptr<RhiScene> &scene = d->scenes[q.slot];
        if (!scene) scene = std::make_unique<RhiScene>(d->rhi.get());
        QRhiTexture *tex = scene->drawView(d->cb, d->take(), q.view, q.items, q.size.width(), q.size.height(), q.look);
        if (!tex) continue;
        auto read = std::make_unique<Read>();
        read->request.slot = q.slot;
        read->request.id = q.id;
        read->size = tex->pixelSize();
        QRhiResourceUpdateBatch *u = d->rhi->nextResourceUpdateBatch();
        u->readBackTexture({tex}, &read->result);
        d->cb->resourceUpdate(u);
        reads.push_back(std::move(read));
    }
    d->sceneViews.clear();
    if (d->pending) d->cb->resourceUpdate(d->take());
    d->rhi->endOffscreenFrame();
    d->previewPictures.clear();
    for (const auto &read : reads)
        if (read->result.data.size() >= qsizetype(read->size.width()) * read->size.height() * 4)
            d->previewPictures.push_back({read->request.slot, read->request.id, read->size, read->result.data});
    d->previewRequests.clear();
    // Keep retired pictures around while frames already submitted to the GPU can still reference them.
    constexpr int framesInFlightSafety = 16;
    std::vector<Impl::RLayer> retired;
    for (auto it = d->layers.begin(); it != d->layers.end();) {
        Impl::RLayer &r = it->second;
        if (r.used || r.image || r.hdr) {
            r.unusedFrames = 0;
            ++it;
        } else if (++r.unusedFrames > framesInFlightSafety) {
            retired.push_back(std::move(r));
            it = d->layers.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = d->outgoing.begin(); it != d->outgoing.end();) {
        Impl::RLayer &r = it->second;
        if (r.used) {
            r.unusedFrames = 0;
            ++it;
        } else if (++r.unusedFrames > framesInFlightSafety) {
            retired.push_back(std::move(r));
            it = d->outgoing.erase(it);
        } else {
            ++it;
        }
    }
    if (!retired.empty()) {
        // These pipelines can retain bindings to the retired layer's textures.
        if (!d->rhi->isDeviceLost()) d->rhi->finish();
        d->pipelines.clear();
        RhiResources::of(d->rhi.get()).resetPipelines();
        retired.clear();
    }
}

static QImage readBack(QRhi *rhi, QRhiTexture *tex, bool deep = false)
{
    if (!rhi || !tex) return {};
    QRhiCommandBuffer *cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return {};
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
    u->readBackTexture({tex}, &result);
    cb->resourceUpdate(u);
    rhi->endOffscreenFrame();
    const int w = result.pixelSize.width(), h = result.pixelSize.height();
    if (w <= 0 || h <= 0) return {};
    QImage img(w, h, QImage::Format_RGBA8888);
    const uchar *data = reinterpret_cast<const uchar *>(result.data.constData());
    if (result.format == QRhiTexture::RGBA16F) { // the 10-bit pictures: half floats, to 8 bits
        const QImage half(data, w, h, w * 8, QImage::Format_RGBA16FPx4);
        img = half.convertToFormat(deep ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    } else if (result.format == QRhiTexture::RGBA32F) {
        const QImage full(data, w, h, w * 16, QImage::Format_RGBA32FPx4);
        img = full.convertToFormat(deep ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    } else if (result.format == QRhiTexture::BGRA8) {
        img = QImage(data, w, h, w * 4, QImage::Format_ARGB32).convertToFormat(QImage::Format_RGBA8888);
    } else {
        img = QImage(data, w, h, w * 4, QImage::Format_RGBA8888).copy();
        if (deep) img = img.convertToFormat(QImage::Format_RGBA64);
    }
    return img.mirrored(false, true); // stored bottom row first
}

QImage RhiRenderer::grabViewport(quint64 viewport, bool deep)
{
    const auto found = d->layers.find(viewport);
    return found == d->layers.end() ? QImage() : readBack(d->rhi.get(), found->second.out.texture(), deep);
}

const void *RhiRenderer::picture(quint64 layer, int which, QSize *size) const
{
    const auto found = d->layers.find(layer);
    if (found == d->layers.end()) return nullptr;
    const QRhiTexture *t = which == 0 ? found->second.final : which == 1 ? found->second.preFx : found->second.raw;
    if (size) *size = t ? t->pixelSize() : QSize();
    return t;
}

QSize RhiRenderer::videoSize(quint64 layer) const
{
    const auto found = d->layers.find(layer);
    return found == d->layers.end() || !found->second.video ? QSize() : QSize(found->second.video->width(), found->second.video->height());
}

quint64 RhiRenderer::videoFramesShown(quint64 layer) const
{
    const auto found = d->layers.find(layer);
    return found == d->layers.end() ? 0 : found->second.videoSerial;
}

QImage RhiRenderer::grabSource(quint64 layer, bool deep)
{
    const auto found = d->layers.find(layer);
    QRhiTexture *raw = found == d->layers.end() ? nullptr : found->second.raw;
    return raw ? readBack(d->rhi.get(), raw, deep) : QImage();
}

bool RhiRenderer::present(quint64 viewport, QWindow *window, bool waitForDisplay)
{
    return d->rhi && d->present(viewport, window, waitForDisplay);
}

void RhiRenderer::releaseOutputs(QWindow *window)
{
    for (auto it = d->outputs.begin(); it != d->outputs.end();)
        it = !window || it->second.window == window ? d->outputs.erase(it) : std::next(it);
}

void RhiRenderer::setPreviewRequests(std::vector<PreviewRequest> requests) { d->previewRequests = std::move(requests); }

std::vector<RhiRenderer::PreviewPicture> RhiRenderer::takePreviews() { return std::exchange(d->previewPictures, {}); }

void RhiRenderer::setCapturePresented(bool on) { d->capturePresented = on; }

QImage RhiRenderer::lastPresented(quint64 viewport) const
{
    const auto it = d->outputs.find(viewport);
    return it == d->outputs.end() ? QImage() : it->second.last;
}

QImage RhiRenderer::grabOutput() { return readBack(d->rhi.get(), d->compositionOut.texture()); }
