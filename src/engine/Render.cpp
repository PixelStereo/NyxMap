#include <random>
// Engine: one frame — the sources are updated, every layer is rendered in dependency order, the
// composition is assembled, then read back for the interface preview and for publishing.
#include <QJsonArray>
#include "EngineInternal.h"
#include "RhiRenderer.h"
#include "RhiTarget.h"
#include "TimecodeClock.h"
#include "ColorInfo.h"
#include "SourceColor.h"

#include <map>
#include <set>
#include <memory>
#include <vector>

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QOffscreenSurface>
#include <QPainter>
#include <QTextLayout>
#include <QWindow>
#include <cmath>
#include <cstring>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif

// The Text generator's picture: the text shown now (typed so far, during a typewriter), wrapped to the layer's
// width, with its alignment (left, center, right, justified; top, middle, bottom), line spacing and letter spacing.
// Filled with `fill` (the text's color, or a countdown's overtime color). Flipped like the images (the sources are
// stored bottom-up).
QImage renderTextLayer(const Layer &l, const QString &text, const QColor &fill, bool inkOnly)
{
    const int w = std::max(1, l.text.width), h = std::max(1, l.text.height);
    QImage img(w, h, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    if (text.isEmpty()) return img.convertToFormat(QImage::Format_RGBA8888);

    QFont font(l.text.font);
    font.setPixelSize(std::max(1, l.text.size));
    font.setLetterSpacing(QFont::AbsoluteSpacing, l.text.letterSpacing);
    font.setBold(l.text.bold);
    font.setItalic(l.text.italic);
    font.setUnderline(l.text.underline);
    font.setStrikeOut(l.text.strike);

    QTextOption option;
    option.setWrapMode(QTextOption::WordWrap);
    option.setAlignment(l.text.align & Qt::AlignHorizontal_Mask ? (l.text.align & Qt::AlignHorizontal_Mask) : Qt::AlignLeft);

    // One layout per paragraph (a line break starts another)
    const QFontMetricsF fm(font);
    const double step = fm.lineSpacing() * std::max(0.1f, l.text.lineHeight);
    struct Line { QTextLayout *layout; QTextLine line; double y; };
    std::vector<std::unique_ptr<QTextLayout>> layouts;
    std::vector<Line> lines;
    double y = 0;
    for (const QString &para : text.split(QChar('\n'))) {
        auto layout = std::make_unique<QTextLayout>(para.isEmpty() ? QStringLiteral(" ") : para, font);
        layout->setTextOption(option);
        layout->beginLayout();
        for (;;) {
            QTextLine line = layout->createLine();
            if (!line.isValid()) break;
            line.setLineWidth(w);
            line.setPosition(QPointF(0, y));
            lines.push_back({layout.get(), line, y});
            y += step;
        }
        layout->endLayout();
        layouts.push_back(std::move(layout));
    }
    const double total = y;
    double top = 0;
    if (l.text.align & Qt::AlignBottom) top = h - total;
    else if (l.text.align & Qt::AlignVCenter) top = (h - total) / 2;

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font);
    // The lines drawn in one color at an offset; the layout's alignment applies inside the width
    auto draw = [&](const QColor &c, double dx, double dy) {
        painter.setPen(c);
        for (const Line &ln : lines) ln.line.draw(&painter, QPointF(dx, top + dy));
    };
    // The outline: the text repeated around its place (a ring of copies), under the fill
    auto drawOutlined = [&](const QColor &outline, const QColor &fill, double dx, double dy, bool withFill) {
        if (l.text.outline > 0.01f) {
            const int n = std::clamp(int(l.text.outline * 6), 16, 48);
            for (int k = 0; k < n; ++k) {
                const double a = 2 * M_PI * k / n;
                draw(outline, dx + std::cos(a) * l.text.outline, dy + std::sin(a) * l.text.outline);
            }
        }
        if (withFill) draw(fill, dx, dy);
    };
    if (inkOnly) { // only the letters, top row first: where the pen goes
        draw(Qt::white, 0, 0);
        painter.end();
        return img.convertToFormat(QImage::Format_RGBA8888);
    }
    if (l.text.shadow) { // the shadow has the shape of the text and its outline, in its own color
        QColor sc = l.text.shadowColor;
        if (l.text.outline > 0.01f) drawOutlined(sc, sc, l.text.shadowX, l.text.shadowY, true);
        else draw(sc, l.text.shadowX, l.text.shadowY);
    }
    drawOutlined(l.text.outlineColor, fill, 0, 0, true);
    painter.end();
    return flippedVertically(img.convertToFormat(QImage::Format_RGBA8888));
}

// The layers the image inputs of a layer's shaders (its generator, its enabled effects) show
void Engine::forEachImageLayer(const Layer &l, const std::function<void(quint64)> &fn) const
{
    if (l.type == SourceType::Tox)
        for (quint64 input : l.tox.inputs) if (input) fn(input);
    auto each = [&](const IsfInstance *inst) {
        for (const IsfInput &in : inst->inputs())
            if (in.imageLayer) fn(in.imageLayer);
    };
    if (l.type == SourceType::Isf && l.generator) each(l.generator.get());
    if (l.effectsEnabled)
        for (const auto &fx : l.effects)
            if (fx->enabled) each(fx.get());
}

void Engine::updateSource(Layer &l, double dt)
{
    // An image has no time: uploaded paused or not (its previous texture is already gone)
    if (!l.pendingImage.isNull()) {
        if (m_rhi) m_rhi->imageArrived(l.id, l.pendingImage); // the QRhi renderer's own picture of it
        l.pendingImage = QImage();
        l.renderPaused = true; // seen at once, even while paused
        l.pendingAutoImage = QImage();
        if (!l.pendingHdr.empty()) { // an HDR file's unclipped pixels
            if (m_rhi) m_rhi->hdrArrived(l.id, l.pendingHdr, l.pendingHdrSize);
            l.pendingHdr.clear();
            l.pendingHdr.shrink_to_fit();
        }
    }
    if (l.toxPlayer && l.tox.playing) l.toxPlayer->advance(m_realDt * timeScale());
    (void)dt;
    if ((l.type != SourceType::Video && l.type != SourceType::Audio) || !l.hasTransport()) return;
    if (l.playing) {
        // Real time (not the clamped animation step): after a render stall, picture and sound stay together.
        l.clock += m_realDt * std::abs(l.speed) * timeScale();
        if (l.atEnd()) { // One-shot / Stop: end of the media (the end, or the start when playing backwards)
            l.clock = l.timeline().firstLeg().clockEnd();
            l.playing = false;
            l.ended = l.mode == PlayMode::Stop; // Stop: black (and silent); One-shot: last frame
        }
    }
    if (l.audio)
        l.audio->setTransport(l.clock, l.playing, std::abs(l.speed) * timeScale(), l.timeline(), l.timelineId, l.audioGain(), m_frameStampNs);
    if (!l.video || timeScale() <= 0) return;
    if (!l.needed && !l.video->isLive()) {
        // Keep short cuts warm. After that, stop consuming the bounded queue so the decoder sleeps.
        l.videoHiddenSeconds += m_realDt;
        if (l.videoHiddenSeconds >= 0.75) {
            l.videoDecodeIdle = true;
            return;
        }
    } else {
        l.videoHiddenSeconds = 0;
        if (l.videoDecodeIdle) {
            l.videoDecodeIdle = false;
            const double frameStep = 1.0 / std::max(1.0, l.video->fps());
            if (!l.ended && std::abs(l.clock - l.frame.pts) > 2.0 * frameStep) {
                // One-shot keeps its last frame. Seeking to the exact out point would skip it.
                const double seek = l.atEnd() ? std::max(0.0, l.clock - frameStep) : l.clock;
                l.video->seek(seek);
                l.frameUploaded = true; // hold the last uploaded picture while the decoder catches up
            }
        }
    }
    // The QRhi renderer takes the frames from the decoder's memory and uploads them itself
    if (l.video->fetch(l.clock, l.frame)) l.frameUploaded = false;
}

// Solo: the soloed layers are kept, with everything inside a soloed group and the groups holding them; the others
// are left out. Nothing soloed: none left out. Viewports are never left out.
std::vector<bool> Engine::soloLeftOut() const
{
    std::vector<bool> out(m_layers.size(), false);
    if (std::none_of(m_layers.begin(), m_layers.end(), [](const auto &l) { return l->solo && !l->isViewport; }))
        return out;
    std::map<quint64, const Layer *> byId;
    for (const auto &l : m_layers) byId.emplace(l->id, l.get());
    auto up = [&](quint64 id) -> const Layer * {
        const auto it = byId.find(id);
        return it == byId.end() ? nullptr : it->second;
    };
    std::set<quint64> kept;
    for (const auto &l : m_layers) {
        bool in = false;
        for (const Layer *p = l.get(); p && !in; p = up(p->parent)) in = p->solo;
        if (!in) continue;
        kept.insert(l->id);
        for (const Layer *g = up(l->parent); g; g = up(g->parent)) kept.insert(g->id);
    }
    for (size_t i = 0; i < m_layers.size(); ++i) out[i] = !m_layers[i]->isViewport && !kept.count(m_layers[i]->id);
    return out;
}

std::vector<Engine::LayerUse> Engine::layerUses(quint64 id) const
{
    Lock lk(&m_mutex);
    std::vector<LayerUse> uses;
    for (const auto &l : m_layers) {
        if (l->id == id) continue;
        const auto add = [&](const QString &how, bool source = false) { uses.push_back({l->id, QStringLiteral("%1 of “%2”").arg(how, l->name), source}); };
        if (l->type == SourceType::Layer && l->sourceLayer == id) add(QStringLiteral("source"), true);
        if (l->matte.layer == id) add(QStringLiteral("matte"));
        if (l->color.maskLayer == id) add(QStringLiteral("color mask"));
        for (const auto &fx : l->effects)
            if (fx->maskLayer == id) add(QStringLiteral("mask of an effect"));
        bool shown = false;
        forEachImageLayer(*l, [&](quint64 used) { shown = shown || used == id; });
        if (shown) add(QStringLiteral("image in a shader"));
        if (l->modelPicture == id) add(QStringLiteral("picture on the model"));
        for (const Fx3dEntry &fx : l->fx3d)
            if (fx.kind == Fx3d::Displace && fx.s.layer == id) add(QStringLiteral("displacement"));
        if (l->sceneEnvironment == id) add(QStringLiteral("environment"));
        if (l->lightLookAt == id) add(QStringLiteral("what is looked at by the light"));
        for (const Animation &a : l->anims)
            if (std::any_of(a.tracks.begin(), a.tracks.end(), [&](const AnimTrack &t) { return t.layer == id; })) add(QStringLiteral("animated by “%1”").arg(a.name));
    }
    const auto other = [&](const QString &what) { uses.push_back({0, what, false}); };
    const std::function<bool(const Interaction &)> involves = [&](const Interaction &it) {
        if (it.layer == id || it.sourceLayer == id) return true;
        for (const auto *list : {&it.onTrue, &it.onFalse})
            for (const InteractionConsequence &c : *list)
                if (c.layer == id) return true;
        return std::any_of(it.xyInputs.begin(), it.xyInputs.end(), involves);
    };
    for (const Interaction &it : m_interactions)
        if (involves(it)) other(QStringLiteral("interaction “%1”").arg(it.label()));
    for (const Animation &a : m_animations)
        if (std::any_of(a.tracks.begin(), a.tracks.end(), [&](const AnimTrack &t) { return t.layer == id; })) other(QStringLiteral("timeline “%1”").arg(a.name));
    for (const Preset &p : m_presets)
        if (std::any_of(p.values.begin(), p.values.end(), [&](const PresetValue &v) { return v.layer == id; })) other(QStringLiteral("preset “%1”").arg(p.name));
    for (int i = 0; i < m_sequencer->count(); ++i) {
        const Sequencer::Sequence sequence = m_sequencer->sequence(i);
        if (sequence.clockLayer == id) other(QStringLiteral("clock of the sequence “%1”").arg(sequence.name));
    }
    return uses;
}

// An active effect may carry state or read from a layer that feeds back into it.
// Keep these chains, live integrations and screen-space reflections warm.
static bool needsContinuousPicture(const Layer &l, bool reflectionsEnabled)
{
    return l.type == SourceType::Tox || l.type == SourceType::Web ||
           (l.generator && !l.generator->canSkipDraw()) || (reflectionsEnabled && l.scene3d && l.scene.reflections > 0) ||
           (l.effectsEnabled && std::any_of(l.effects.begin(), l.effects.end(), [](const auto &fx) { return fx->enabled; }));
}

bool Engine::layerPreviewed(const Layer &l) const
{
    return l.enabled && l.parentEnabled &&
           (l.id == m_previewId || std::any_of(m_previewSlots.begin(), m_previewSlots.end(),
                                               [&](const auto &p) { return p.id == l.id; }));
}

void Engine::forEachRenderDependency(const Layer &l, const std::function<void(size_t, int)> &use) const
{
    auto add = [&](quint64 id, int stage = -1, bool viewport = false) {
        const size_t row = renderLayerIndex(id);
        if (row < m_layers.size() && (viewport || !m_layers[row]->isViewport)) use(row, stage);
    };
    if (l.type == SourceType::Layer) add(l.sourceLayer, -1, true);
    if (l.effectsEnabled)
        for (const auto &fx : l.effects)
            if (fx->enabled) add(fx->maskLayer, fx->maskStage);
    if (l.color.maskLayer && !l.color.effective().isIdentity()) add(l.color.maskLayer);
    add(l.matte.layer);
    forEachImageLayer(l, [&](quint64 id) { add(id); });
    if (l.type == SourceType::Model && l.model) {
        add(l.modelPicture, l.modelPictureStage);
        for (const Fx3dEntry &fx : l.fx3d)
            if (fx.kind == Fx3d::Displace && fx.enabled) add(fx.s.layer, fx.s.stage);
    }
    if (l.scene3d) add(l.sceneEnvironment, l.sceneEnvironmentStage);
}

void Engine::markNeeded()
{
    const size_t n = m_layers.size();
    m_renderChildren.resize(n);
    for (auto &children : m_renderChildren) children.clear();
    const std::vector<bool> leftOut = soloLeftOut();
    const bool reflectionsEnabled = m_quality3d.reflections > 0;
    bool soloHasSound = false;
    QRectF outputBounds(0, 0, 1, 1); // the composition always exists, independently of viewport routing
    const double cw = std::max(1, m_compSize.width()), ch = std::max(1, m_compSize.height());
    for (size_t i = 0; i < n; ++i) {
        Layer &l = *m_layers[i];
        l.needed = l.referenced = false;
        l.stagesWanted.clear();
        l.soloHidden = leftOut[i];
        soloHasSound = soloHasSound || (!leftOut[i] && l.audio && !l.isGroup && !l.isViewport);
        const size_t parent = renderLayerIndex(l.parent);
        l.parentEnabled = true;
        if (parent < i) {
            m_renderChildren[parent].push_back(i);
            const Layer &group = *m_layers[parent];
            l.parentEnabled = !group.isGroup || (group.enabled && group.parentEnabled);
        }
        if (l.isViewport && l.enabled && !l.testCard) {
            const Mapping::Rect r = l.mapping.rect(m_compSize);
            // A circumscribed square is a conservative bound for a rotated viewport.
            const double side = r.angle == 0 ? 0 : std::hypot(r.w, r.h);
            QRectF view(0, 0, (r.angle == 0 ? r.w : side) / cw, (r.angle == 0 ? r.h : side) / ch);
            view.moveCenter(QPointF(r.center.x() / cw, r.center.y() / ch));
            outputBounds = outputBounds.united(view);
        }
    }
    const bool anyLeftOut = std::find(leftOut.begin(), leftOut.end(), true) != leftOut.end();
    for (size_t i = 0; i < n; ++i) m_layers[i]->soloMuted = anyLeftOut && soloHasSound && leftOut[i];

    auto visiblePicture = [&](const Layer &l) {
        if (!l.enabled || l.soloHidden || l.opacity <= 0.0f || !l.hasPicture() || l.isMappingZone) return false;
        // Ordinary group members draw into the composition-sized group target.
        const QRectF view = l.parent ? QRectF(0, 0, 1, 1) : outputBounds;
        const Mapping &mapping = effectiveMapping(l);
        if (view.contains(mapping.position)) return true; // common case; conservatively keep it
        const QPointF first = mapping.corner(0);
        double left = first.x(), right = left, top = first.y(), bottom = top;
        for (int i = 1; i < 4; ++i) {
            const QPointF p = mapping.corner(i);
            left = std::min(left, p.x()); right = std::max(right, p.x());
            top = std::min(top, p.y()); bottom = std::max(bottom, p.y());
        }
        if (!(right < view.left() - 1 / cw || left > view.right() + 1 / cw ||
              bottom < view.top() - 1 / ch || top > view.bottom() + 1 / ch)) return true;
        // The corner bounds are only exact for affine mapping. A pin or a
        // warped mesh can pull pixels back into the output between corners.
        return std::any_of(std::begin(mapping.pins), std::end(mapping.pins), [](const QPointF &p) { return !p.isNull(); }) ||
               std::any_of(mapping.offsets.begin(), mapping.offsets.end(), [](const QPointF &p) { return !p.isNull(); });
    };

    std::function<void(size_t)> need;
    const auto use = [&](size_t row, int stage) {
        Layer &source = *m_layers[row];
        source.referenced = true;
        if (stage > 0) source.stagesWanted.insert(stage);
        need(row);
    };
    std::function<void(size_t)> needModels = [&](size_t row) {
        for (const size_t child : m_renderChildren[row]) {
            Layer &member = *m_layers[child];
            if (!member.enabled) continue;
            if (member.merge3d) needModels(child);
            else if (!member.soloHidden && member.type == SourceType::Model) {
                need(child);
                if (const Layer *o = member.instanceOf ? originalOf(member) : nullptr) // what it shows: its original's
                    if (const int row = indexOfId(o->id); row >= 0) need(size_t(row));
            }
        }
    };
    need = [&](size_t row) {
        Layer &l = *m_layers[row];
        if (l.needed) return; // dependency cycle
        l.needed = true;
        if (!l.isViewport || (l.enabled && !l.testCard)) forEachRenderDependency(l, use);
        if (l.scene3d) needModels(row);
        else if (l.isGroup && !l.merge3d)
            for (const size_t child : m_renderChildren[row]) {
                const Layer &member = *m_layers[child];
                if (visiblePicture(member) || (member.enabled && !member.soloHidden && needsContinuousPicture(member, reflectionsEnabled)))
                    need(child);
            }
    };

    for (size_t i = 0; i < n; ++i) {
        const Layer &l = *m_layers[i];
        if (l.isViewport || (!l.parent && visiblePicture(l)) || layerPreviewed(l) ||
            (l.enabled && l.parentEnabled && !l.soloHidden && needsContinuousPicture(l, reflectionsEnabled)))
            need(i);
    }
    for (const auto &slot : m_previewSlots) {
        const size_t row = renderLayerIndex(slot.id);
        if (slot.sceneView && row < n && (m_layers[row]->scene3d || m_layers[row]->merge3d)) needModels(row);
    }
    for (auto &[id, transition] : m_transitions) {
        if (const size_t row = renderLayerIndex(id); row < n) {
            need(row);
            transition->from->parentEnabled = m_layers[row]->parentEnabled;
        }
        transition->from->needed = true;
        forEachRenderDependency(*transition->from, use);
    }

    // A viewport's own picture (not its output) shown by something else: a layer taking it as its source, a mask, a
    // matte, a shader's image, a model, the inspector's source preview
    for (auto &l : m_layers)
        if (l->isViewport) l->vpPictureUsed = l->referenced || !l->stagesWanted.empty() || l->id == m_previewId;
    const auto showsViewport = [&](quint64 id) {
        const size_t row = renderLayerIndex(id);
        if (row < n && m_layers[row]->isViewport) m_layers[row]->vpPictureUsed = true;
    };
    const auto showsViewports = [&](const Layer &l) {
        showsViewport(l.sourceLayer);
        showsViewport(l.matte.layer);
        showsViewport(l.color.maskLayer);
        for (const auto &fx : l.effects) showsViewport(fx->maskLayer);
        forEachImageLayer(l, showsViewport);
        showsViewport(l.modelPicture);
        for (const Fx3dEntry &fx : l.fx3d)
            if (fx.kind == Fx3d::Displace) showsViewport(fx.s.layer);
        showsViewport(l.sceneEnvironment);
    };
    for (auto &l : m_layers) showsViewports(*l);
    for (auto &[id, transition] : m_transitions) showsViewports(*transition->from);

    const bool paused = timeScale() <= 0;
    for (auto &l : m_layers) {
        if (paused) {
            if (l->needed && l->pictureCulled && !needsContinuousPicture(*l, reflectionsEnabled))
                l->renderPaused = true;
        } else {
            l->pictureCulled = !l->needed && l->hasPicture();
        }
    }
}

size_t Engine::renderLayerIndex(quint64 id) const
{
    if (!id) return m_layers.size();
    const auto at = std::lower_bound(m_renderLayerIndices.begin(), m_renderLayerIndices.end(), id,
                                     [](const auto &entry, quint64 key) { return entry.first < key; });
    return at != m_renderLayerIndices.end() && at->first == id ? at->second : m_layers.size();
}

QMatrix4x4 Engine::mergesOf(const Layer &object)
{
    std::vector<const Layer *> chain; // its Merge 3Ds, the outermost first
    for (quint64 up = object.parent; up;) {
        const Layer *g = layer(indexOfId(up));
        if (!g || !g->merge3d) break;
        chain.insert(chain.begin(), g);
        up = g->parent;
    }
    QMatrix4x4 parent;
    for (const Layer *g : chain) parent = followFrame(*g, parent) * g->modelPlacement.transform();
    return parent;
}

QMatrix4x4 Engine::pathMatrix(const Layer &path) { return mergesOf(path) * path.modelPlacement.transform(); }

// All its Follow Path FX that are on, in order (each from where the one before left it)
QMatrix4x4 Engine::followFrame(const Layer &object, const QMatrix4x4 &parent)
{
    QMatrix4x4 frame = parent;
    for (const Fx3dEntry &fx : object.fx3d)
        if (fx.kind == Fx3d::FollowPath && fx.enabled) frame = followFrame(object, fx, frame);
    return frame;
}

QMatrix4x4 Engine::followFrame(const Layer &object, const Fx3dEntry &fx, const QMatrix4x4 &parent)
{
    const Layer *path = fx.s.layer ? layer(indexOfId(fx.s.layer)) : nullptr;
    if (!path || path->type != SourceType::Path || !path->path3d.isPath() || path == &object) return parent;
    // Paths in Merge 3Ds that follow paths: a chain, not a loop (A in a Merge 3D following B, B in one following A)
    static thread_local int depth = 0;
    if (depth > 8) return parent;
    ++depth;
    const QMatrix4x4 m = pathMatrix(*path);
    --depth;
    const double t = double(fx.s.position) + fx.travel;
    QMatrix4x4 frame;
    frame.translate(m.map(path->path3d.along(t)));
    if (fx.s.orient) { // its +Z the way it goes, its +Y as up as can be
        const double eps = 0.002;
        QVector3D forward = m.map(path->path3d.along(t + eps)) - m.map(path->path3d.along(t - eps));
        if (forward.lengthSquared() > 1e-12f) {
            forward.normalize();
            QVector3D up(0, 1, 0);
            if (std::abs(QVector3D::dotProduct(up, forward)) > 0.999f) up = QVector3D(1, 0, 0);
            const QVector3D right = QVector3D::crossProduct(up, forward).normalized();
            up = QVector3D::crossProduct(forward, right);
            QMatrix4x4 turn;
            turn.setColumn(0, QVector4D(right, 0));
            turn.setColumn(1, QVector4D(up, 0));
            turn.setColumn(2, QVector4D(forward, 0));
            frame *= turn;
        }
    }
    return frame;
}

std::vector<QMatrix4x4> Engine::duplicateFrames(const Fx3dEntry &fx, const QMatrix4x4 &frame)
{
    const Layer *path = fx.s.layer ? layer(indexOfId(fx.s.layer)) : nullptr;
    if (!path || path->type != SourceType::Path) return {frame};
    // On the Path's points, where the Path is (whatever holds the model); its Follow Path does not apply then
    const QMatrix4x4 m = pathMatrix(*path);
    const auto points = path->path3d.pointsLocal();
    const auto ways = path->path3d.directionsLocal();
    std::mt19937 rng{static_cast<uint32_t>(fx.s.seed)};
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    std::vector<QMatrix4x4> out;
    out.reserve(points.size());
    for (size_t k = 0; k < points.size(); ++k) {
        QMatrix4x4 copy;
        copy.translate(m.map(points[k]));
        if (fx.s.align == 1) { // its +Z the way the point goes, +Y up (on a sphere: its +Y outward)
            QVector3D way = m.mapVector(ways[k]);
            if (way.lengthSquared() > 1e-12f) {
                way.normalize();
                const bool outward = path->path3d.kind == Path3D::Sphere || !path->path3d.isPath();
                QVector3D up = outward ? way : QVector3D(0, 1, 0);
                QVector3D ahead = outward ? QVector3D(0, 0, 1) : way;
                if (std::abs(QVector3D::dotProduct(up, ahead)) > 0.999f) ahead = QVector3D(1, 0, 0);
                const QVector3D right = QVector3D::crossProduct(up, ahead).normalized();
                ahead = QVector3D::crossProduct(right, up);
                QMatrix4x4 turn;
                turn.setColumn(0, QVector4D(right, 0));
                turn.setColumn(1, QVector4D(up, 0));
                turn.setColumn(2, QVector4D(ahead, 0));
                copy *= turn;
            }
        }
        const float spin = (unit(rng) * 2 - 1) * fx.s.randomRotation;
        const float size = fx.s.scale * (1 + (unit(rng) * 2 - 1) * fx.s.randomScale);
        copy.rotate(spin, 0, 1, 0);
        copy.scale(std::max(0.001f, size));
        out.push_back(copy);
    }
    return out;
}

QVector3D Engine::cameraPosition(const Layer &camera)
{
    const ModelPlacement &p = camera.modelPlacement;
    const QMatrix4x4 parent = mergesOf(camera), frame = followFrame(camera, parent);
    if (frame != parent) return frame.map(QVector3D()); // its Follow Path FX: on the path's point
    return parent.map(QVector3D(p.position[0], p.position[1], p.position[2]));
}

SceneSettings Engine::placedCamera(const Layer &scene)
{
    const Layer *cam = scene.activeCamera ? layer(indexOfId(scene.activeCamera)) : nullptr;
    if (!cam || cam->type != SourceType::Camera) return scene.scene; // its own orbit camera
    return cameraView(*cam, scene.scene);
}

SceneSettings Engine::cameraView(const Layer &camera, const SceneSettings &base)
{
    SceneSettings s = base;
    const Layer *cam = &camera;
    const CameraSettings &c = cam->camera;
    const QVector3D eye = cameraPosition(*cam);
    QVector3D target;
    if (c.lookAt) {
        const Layer *looked = cam->lookAtObject ? layer(indexOfId(cam->lookAtObject)) : nullptr;
        target = looked ? placeOf3dObject(*looked) : QVector3D(c.lookPoint[0], c.lookPoint[1], c.lookPoint[2]);
    } else {
        target = eye - Camera3D::aim(c.yaw, c.pitch); // Camera3D::of puts the eye at target + aim(yaw, pitch) * distance
    }
    QVector3D from = eye - target;
    if (from.lengthSquared() < 1e-8f) from = QVector3D(0, 0, 0.01f);
    for (int a = 0; a < 3; ++a) s.target[size_t(a)] = target[a];
    s.distance = from.length();
    s.fov = c.fov;
    Camera3D::anglesOf(from.normalized(), &s.yaw, &s.pitch);
    return s;
}

std::vector<QMatrix4x4> Engine::placementFrames(const Layer &object, const QMatrix4x4 &parent, bool copies)
{
    std::vector<QMatrix4x4> frames{parent};
    for (const Fx3dEntry &fx : object.fx3d) {
        if (isShapeFx3d(fx.kind) || !fx.enabled) continue;
        if (fx.kind == Fx3d::FollowPath) {
            for (QMatrix4x4 &frame : frames) frame = followFrame(object, fx, frame);
        } else if (fx.kind == Fx3d::Transform) {
            const QMatrix4x4 t = fx.s.transform();
            for (QMatrix4x4 &frame : frames) frame *= t;
        } else if (fx.kind == Fx3d::Duplicate && copies) {
            std::vector<QMatrix4x4> next;
            for (const QMatrix4x4 &frame : frames)
                for (const QMatrix4x4 &copy : duplicateFrames(fx, frame)) next.push_back(copy);
            frames.swap(next);
        }
    }
    return frames;
}

QVector3D Engine::lightPosition(const Layer &light, const QMatrix4x4 &parent)
{
    // Its Follow Path FX: on the path's point
    if (light.fx3dOn(Fx3d::FollowPath)) {
        const QMatrix4x4 frame = followFrame(light, parent);
        if (frame != parent) return frame.map(QVector3D());
    }
    return parent.map(QVector3D(light.light.position[0], light.light.position[1], light.light.position[2]));
}

QVector3D Engine::placeOf3dObject(const Layer &object)
{
    QMatrix4x4 parent = mergesOf(object);
    if (object.type == SourceType::Light) return lightPosition(object, parent);
    if (object.type == SourceType::Path) return pathMatrix(object).map(QVector3D());
    if (object.type == SourceType::Camera) return cameraPosition(object);
    if (const auto model = object.type == SourceType::Model ? modelOf(object) : nullptr) {
        parent = placementFrames(object, parent, false).front();
        ModelPlacement p = object.modelPlacement;
        if (const Layer *o = object.instanceOf ? originalOf(object) : nullptr) p.fit = o->modelPlacement.fit;
        return (parent * model->matrix(p)).map(model->center());
    }
    parent = followFrame(object, parent);
    const ModelPlacement &p = object.modelPlacement;
    return parent.map(QVector3D(p.position[0], p.position[1], p.position[2]));
}

LightSettings Engine::placedLight(const Layer &m, const QMatrix4x4 &parent, QVector3D *target)
{
    LightSettings light = m.light;
    const QVector3D at = lightPosition(m, parent);
    for (int a = 0; a < 3; ++a) light.position[size_t(a)] = at[a];
    QVector3D points = parent.mapVector(Camera3D::aim(light.yaw, light.pitch));
    if (light.lookAt) {
        const Layer *object = m.lightLookAt ? layer(indexOfId(m.lightLookAt)) : nullptr;
        const QVector3D seen = object ? placeOf3dObject(*object) : QVector3D(light.lookPoint[0], light.lookPoint[1], light.lookPoint[2]);
        if (target) *target = seen;
        if ((seen - at).lengthSquared() > 1e-8f) points = seen - at; // on its target: as it was
    }
    Camera3D::anglesOf(points, &light.yaw, &light.pitch);
    return light;
}

// What a 3D view of a slot shows: the objects of its scene (or Merge 3D) and the view's camera and background (render
// thread, lock held)
void Engine::sceneViewOf(const PreviewSlot &slot, int index, std::vector<Scene3D::Item> &items, SceneSettings &view)
{
    const Layer &layer = *m_layers[size_t(index)];
    std::vector<Layer *> members;
    QMatrix4x4 within; // a Merge 3D: where it is in its scene (what holds it)
    const Layer *scene = &layer;
    if (layer.merge3d) {
        members.push_back(m_layers[size_t(index)].get()); // itself, with what it holds
        within = mergesOf(layer);
        for (quint64 up = layer.parent; up; ) {
            const int aboveIndex = indexOfId(up);
            const Layer *above = aboveIndex >= 0 ? m_layers[size_t(aboveIndex)].get() : nullptr;
            if (!above) break;
            scene = above;
            if (above->scene3d) break;
            up = above->parent;
        }
    } else {
        for (auto &m : m_layers)
            if (m->parent == layer.id) members.push_back(m.get());
    }
    std::vector<Scene3D::Lamp> lamps;
    collectScene(members, items, lamps, within);
    view = scene->scene; // the view's camera and background (a Merge 3D: its scene's)
    for (int k = 0; k < 3; ++k) view.target[size_t(k)] = slot.camera.target[size_t(k)];
    view.distance = slot.camera.distance;
    view.yaw = slot.camera.yaw;
    view.pitch = slot.camera.pitch;
    view.fov = slot.camera.fov;
    view.background = slot.camera.background;
}

void Engine::collectScene(const std::vector<Layer *> &members, std::vector<Scene3D::Item> &items, std::vector<Scene3D::Lamp> &lamps,
                          const QMatrix4x4 &parent)
{
    for (Layer *m : members) {
        if (m->merge3d) { // what it holds, under its transform and those above it; hidden, all of it
            if (!m->enabled) continue;
            std::vector<Layer *> inside;
            for (auto &o : m_layers)
                if (o->parent == m->id) inside.push_back(o.get());
            collectScene(inside, items, lamps, followFrame(*m, parent) * m->modelPlacement.transform());
            continue;
        }
        if (!m->enabled || m->soloHidden) continue;
        if (m->type == SourceType::Model && (m->model || m->instanceOf)) {
            // What it looks like: its own, or (an Instance) its original's: the same meshes, material, picture,
            // displacement, shading and detail; its own transform, 3D FX and, with its own material, colours
            const Layer *look = m->instanceOf ? originalOf(*m) : m;
            const std::shared_ptr<Model3D> model = look ? look->model : nullptr;
            if (!model) continue;
            if (look == m) model->setSmooth(m->modelPlacement.shading == 1); // an original sets its shared mesh
            const Layer *pic = look->modelPicture ? layer(indexOfId(look->modelPicture)) : nullptr;
            // Its Displace: the first that is on and has a picture (one picture at a time)
            const Fx3dEntry *displaceFx = nullptr;
            for (const Fx3dEntry &fx : look->fx3d)
                if (fx.kind == Fx3d::Displace && fx.enabled && fx.s.layer && !displaceFx) displaceFx = &fx;
            const Layer *disp = displaceFx ? layer(indexOfId(displaceFx->s.layer)) : nullptr;
            auto placement = look->modelPlacement;
            if (look != m) {
                const ModelPlacement &own = m->modelPlacement;
                placement.position = own.position;
                placement.rotation = own.rotation;
                placement.scale = own.scale;
                placement.axisScale = own.axisScale;
                if (m->instanceOwnMaterial) {
                    placement.color = own.color;
                    placement.metal = own.metal;
                    placement.rough = own.rough;
                    placement.unlit = own.unlit;
                }
            }
            const bool ownsMaterial = look->material && !(look != m && m->instanceOwnMaterial);
            if (ownsMaterial) MaterialTextureCache::applyParameters(*look->material, placement);
            bool displacement = disp != nullptr; // a layer's picture pushing its points (QRhi finds it from the layer)
            float amount = displaceFx ? displaceFx->s.amount : 0, mid = displaceFx ? displaceFx->s.mid : 0;
            if (!displacement && ownsMaterial && !look->material->images[HeightMap].isNull() && look->material->height != 0) {
                displacement = true; // the height map of its material
                placement.materialHeight = true;
                amount = look->material->height;
                mid = 0;
            }
            // Its shape FX, in the order they are listed (a height map of its material pushes its points as a Displace first)
            std::vector<Scene3D::Item::ShapeOp> shape;
            bool displaced = false;
            const QVector3D size = model->size();
            const float widest = std::max({size.x(), size.y(), size.z()});
            const auto along = [&size](int axis) { return axis == 0 ? size.x() : axis == 1 ? size.y() : size.z(); };
            for (const Fx3dEntry &fx : look->fx3d) {
                if (!isShapeFx3d(fx.kind) || !fx.enabled) continue;
                const Fx3dSettings &s = fx.s;
                Scene3D::Item::ShapeOp op;
                op.kind = fx.kind;
                if (fx.kind == Fx3d::Displace) {
                    if (&fx != displaceFx || !displacement) continue;
                    displaced = true;
                } else if (fx.kind == Fx3d::Bend) {
                    if (s.amount == 0) continue;
                    op.a = {qDegreesToRadians(std::clamp(s.amount, -359.0f, 359.0f)), 0, 0, 0};
                } else if (fx.kind == Fx3d::Twist) {
                    op.a = {qDegreesToRadians(s.angle), float(s.axis), 0, 0};
                } else if (fx.kind == Fx3d::Taper) {
                    op.a = {s.amount, float(s.axis), 0, 0};
                } else if (fx.kind == Fx3d::Wave) {
                    op.a = {s.amount * 0.5f * widest, s.length * along(s.axis), s.phase, float(s.axis)};
                } else if (fx.kind == Fx3d::Noise) {
                    // Its values: a new field of noise at each whole step, the one before it turning into it as the step comes
                    // (not at all, linearly or smoothly); the step it is at is kept small so the noise stays precise
                    const double at = fx.travel + double(s.phase);
                    const double step = std::floor(at);
                    op.a = {s.amount * 0.5f * widest, s.scale, float(at - step), float(s.interp)};
                    op.b = {float(std::fmod(step, 256.0) + (step < 0 ? 256.0 : 0.0)), float(s.seed), 0, 0};
                }
                shape.push_back(op);
            }
            if (displacement && !displaced) shape.insert(shape.begin(), {Fx3d::Displace, {}}); // a material's height map, with no Displace listed
            if (look == m) model->setDetail(placement.meshDetail(!shape.empty()));
            // Its placement FX: Follow Path, Duplicate (a copy on each point of its Path: the same meshes, drawn again,
            // not built again) and Transform 3D, in the order they are listed
            for (const QMatrix4x4 &frame : placementFrames(*m, parent, true)) {
                Scene3D::Item item{model, placement, amount, mid, frame};
                item.shape = shape;
                if (placement.sharedMaterial && look->material) item.material = look->material;
                item.pictureLayer = pic ? pic->id : 0;
                item.pictureStage = look->modelPictureStage;
                item.displaceLayer = disp ? disp->id : 0;
                item.displaceStage = displaceFx ? displaceFx->s.stage : -1;
                items.push_back(item);
            }
        } else if (m->type == SourceType::Light) {
            lamps.push_back({placedLight(*m, parent), m->opacity});
        }
    }
}

// Whether a viewport's output is its layers composited and nothing more (see renderViewport)
bool Engine::viewportIsPlain(const Layer &v, const std::vector<Layer *> &shown) const
{
    if (v.vpPictureUsed || v.opacity != 1.0f || v.roiMode || v.roi.normalized() != Layer::fullRoi()) return false;
    if (!v.color.effective().isIdentity() || v.matte.layer || masks::any(v.masks, Mask::Attach::Surface)) return false;
    if (v.effectsEnabled && std::any_of(v.effects.begin(), v.effects.end(), [](const auto &fx) { return fx->enabled; })) return false;
    const QString &targetSpace = v.outputSpace == QLatin1String("same") ? v.outputInputSpace : v.outputSpace;
    if (targetSpace != v.outputInputSpace || v.outputExposure != 0) return false;
    return std::none_of(shown.begin(), shown.end(), [](const Layer *l) { return l->blend == BlendMode::Difference; });
}

double Engine::nextDt()
{
    const qint64 now = m_clock.nsecsElapsed();
    double dt = (now - m_lastNs) / 1e9;
    m_lastNs = now;
    m_frameStampNs = AudioStream::clockNs();
    const FrameStep step = FrameStep::fromElapsed(dt, m_threaded);
    m_realDt = step.elapsed;
    dt = step.shaderDelta;
    if (dt > 0) m_fps = m_fps.load() * 0.95 + (1.0 / dt) * 0.05;
    return dt;
}

void Engine::frame(double dt)
{
    const uint64_t lockRequested = RealtimeTiming::nowNs();
    // In threaded mode, keep serving OpenGL tasks while the UI holds the lock:
    // it may wait on a task while holding it, without deadlock.
    if (m_threaded) {
        while (!m_mutex.tryLock(2)) {
            runPendingTasks();
            if (m_quit) return;
        }
    } else {
        m_mutex.lock();
    }
    const uint64_t lockAcquired = RealtimeTiming::nowNs();
    m_renderLockWait.record(lockAcquired - lockRequested);

    // Manual faders use command-relative wall time, including render-lock waits.
    const double faderDt = m_compositionFaderClock.step(lockAcquired, m_threaded);
    const double blackoutDt = m_blackoutClock.step(lockAcquired, m_threaded);
    // Composition opacity
    double lvl = m_compositionOpacity.load();
    if (m_compositionOpacitySpeed <= 0.0) {
        lvl = m_compositionOpacityTarget;
    } else {
        const double step = faderDt * m_compositionOpacitySpeed;
        lvl = lvl < m_compositionOpacityTarget ? std::min(m_compositionOpacityTarget, lvl + step) : std::max(m_compositionOpacityTarget, lvl - step);
    }
    m_compositionOpacity = lvl;
    double black = m_blackLevel.load();
    if (m_blackSpeed <= 0.0) {
        black = m_blackTarget;
    } else {
        const double step = blackoutDt * m_blackSpeed;
        black = black < m_blackTarget ? std::min(m_blackTarget, black + step) : std::max(m_blackTarget, black - step);
    }
    m_blackLevel = black;

    IsfRenderContext rc;
    const double scale = timeScale();
    rc.dt = dt * scale;
    rc.paused = scale <= 0;
    rc.elapsed = m_realDt * scale;

    if (!m_fadesManual) {
        const double k = timeScale(); // paused or slowed: the fades, transitions and timelines move at that pace
        stepShowRecalls(m_realDt * k);
    }
    stepInteractions(m_realDt); // after the animations: a controller moved wins (its own time: it follows the hand)
    followSequenceClocks();     // the steps a clock (a soundtrack's playhead, a timecode) got to: their GO
    // Cues can replace or reorder layers. Reuse the sorted lookup while the ids stay in the same rows.
    if (m_renderLayerIndices.size() != m_layers.size() ||
        !std::all_of(m_renderLayerIndices.begin(), m_renderLayerIndices.end(), [this](const auto &entry) {
            return m_layers[entry.second]->id == entry.first;
        })) {
        m_renderLayerIndices.clear();
        m_renderLayerIndices.reserve(m_layers.size());
        for (size_t i = 0; i < m_layers.size(); ++i)
            m_renderLayerIndices.emplace_back(m_layers[i]->id, i);
        std::sort(m_renderLayerIndices.begin(), m_renderLayerIndices.end());
    }
    m_renderLayerIndexActive = true;
    if (!m_rhi && !m_rhiFailed) { // the QRhi renderer: made by this thread, used by it from then on
        m_rhi = std::make_unique<RhiRenderer>(*this);
        QString err;
        if (!m_rhi->init(&err)) {
            qWarning("QRhi renderer: %s", qPrintable(err));
            m_rhi.reset();
            m_rhiFailed = true;
        }
    }
    RhiTarget::setWorkingFormat(effectiveRender().depth >= 10 ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8); // the 10-bit precision
    markNeeded();
    for (auto &l : m_layers) updateSource(*l, dt);
    for (auto &[id, t] : m_transitions) updateSource(*t->from, dt); // the outgoing sources play on
    releaseRetired(false); // transitions over whose decoders are closed by now

    if (!m_rhi) { // no graphics API for QRhi here: nothing is drawn
        m_renderLayerIndexActive = false;
        m_mutex.unlock();
        ++m_frameCount;
        return;
    }
    { // the frame through QRhi (the previews and the publishers' pictures are read back below)
        // The pictures the interface asked for come back as pixels
        std::vector<RhiRenderer::PreviewRequest> asked;
        for (size_t i = 0; i < m_previewSlots.size(); ++i) {
            const PreviewSlot &slot = m_previewSlots[i];
            if (slot.asked && !slot.sceneView && (!slot.id || indexOfId(slot.id) >= 0)) asked.push_back({int(i), slot.id, 1024});
        }
        // The pixel readers' pictures, read back too (the offscreen frame completes at once, so they are this frame's)
        constexpr int sonificationSlot = 1000, pixelMappingSlot = 2000, sourcePreviewSlot = 4000, publishSlot = 5000, compositionSlot = 6000;
        std::vector<quint64> listened; // the viewports (0: the composition) sonifications listen to, by slot
        {
            std::set<quint64> seen;
            for (const auto &layer : m_layers) {
                if (!layer->audio || !layer->audio->generator() || !layer->audio->generator()->sonification()) continue;
                const Layer *viewport = nullptr;
                for (const auto &candidate : m_layers) {
                    if (!candidate->isViewport) continue;
                    if (!viewport) viewport = candidate.get();
                    if (layer->sonificationViewport && candidate->id == layer->sonificationViewport) {
                        viewport = candidate.get();
                        break;
                    }
                }
                if (seen.insert(viewport ? viewport->id : 0).second) listened.push_back(viewport ? viewport->id : 0);
            }
        }
        for (size_t i = 0; i < listened.size(); ++i) {
            RhiRenderer::PreviewRequest q{sonificationSlot + int(i), listened[i], 16};
            q.exact = QSize(16, 16);
            asked.push_back(q);
        }
        if (!m_fixturePoints.empty()) asked.push_back({pixelMappingSlot, 0, 1 << 20});
        const bool sourcePreviewDue = m_previewId && m_frameStampNs >= m_previewNextNs;
        if (sourcePreviewDue) {
            m_previewNextNs = m_frameStampNs + 66'666'667;
            RhiRenderer::PreviewRequest q{sourcePreviewSlot, m_previewId, std::max(1, m_previewSide)};
            q.tap = RhiRenderer::PreviewRequest::Raw;
            asked.push_back(q);
        }
        if (qint64(RealtimeTiming::nowNs()) < m_compositionWantedUntilNs.load()) asked.push_back({compositionSlot, 0, 1600}); // the mapping view
        // The viewports being published (NDI, OMT, DeckLink, Syphon, Spout: pixels)
        std::vector<std::pair<quint64, PublishSettings>> publish;
        std::vector<quint64> published; // by slot
        std::vector<std::optional<Timecode>> atc;
        for (const auto &l : m_layers)
            if (l->isViewport) {
                publish.push_back({l->id, l->vpPublish});
                if (m_publishing.wantsPixels(l->id) || m_publishing.wantsShare(l->id)) {
                    asked.push_back({publishSlot + int(published.size()), l->id, 1 << 20});
                    published.push_back(l->id);
                    const auto tc = m_atc.find(l->id);
                    atc.push_back(tc != m_atc.end() ? tc->second->now() : std::nullopt);
                }
            }
        m_rhi->setPreviewRequests(std::move(asked));
        { // the 3D views of the cells (the flat and the unlit looks), drawn through QRhi
            std::vector<RhiRenderer::SceneViewRequest> views;
            for (size_t i = 0; i < m_previewSlots.size(); ++i) {
                const PreviewSlot &slot = m_previewSlots[i];
                if (!slot.asked || !slot.sceneView || !slot.id) continue;
                const int index = indexOfId(slot.id);
                if (index < 0 || !(m_layers[size_t(index)]->scene3d || m_layers[size_t(index)]->merge3d)) continue;
                RhiRenderer::SceneViewRequest q;
                q.slot = int(i);
                q.id = slot.id;
                q.size = slot.sceneSize;
                q.look = slot.sceneLook;
                sceneViewOf(slot, index, q.items, q.view);
                views.push_back(std::move(q));
            }
            m_rhi->setSceneViews(std::move(views));
        }
        m_rhi->render(rc);
        std::map<int, std::array<float, 3>> listenedColors;
        bool sourcePreviewGiven = false;
        {
            std::array<QImage, 4> images;
            std::array<quint64, 4> ids{};
            QImage composition;
            for (const auto &p : m_rhi->takePreviews()) {
                if (p.slot >= compositionSlot) { // the mapping view
                    composition = QImage(reinterpret_cast<const uchar *>(p.pixels.constData()), p.size.width(), p.size.height(),
                                         p.size.width() * 4, QImage::Format_RGBA8888)
                                      .copy();
                    continue;
                }
                if (p.slot >= publishSlot) {
                    const size_t k = size_t(p.slot - publishSlot);
                    if (k >= published.size()) continue;
                    const auto *px = reinterpret_cast<const unsigned char *>(p.pixels.constData());
                    m_publishing.submitPixels(published[k], px, p.size.width(), p.size.height(), m_fps.load(), m_frameCount.load(), atc[k]);
                    if (m_publishing.wantsShare(published[k])) m_publishing.share(published[k], px, p.size.width(), p.size.height());
                    continue;
                }
                if (p.slot >= sourcePreviewSlot) {
                    QImage img(reinterpret_cast<const uchar *>(p.pixels.constData()), p.size.width(), p.size.height(), QImage::Format_RGBA8888);
                    std::lock_guard<std::mutex> lk(m_previewMutex);
                    m_previewImage = flippedVertically(img.copy());
                    m_previewImageId = p.id;
                    sourcePreviewGiven = true;
                    continue;
                }
                if (p.slot >= pixelMappingSlot) { // pixel mapping samples the composition's pixels
                    m_mappingPixels = p.pixels;
                    m_mappingSize = p.size;
                    m_mappingFresh = true;
                    continue;
                }
                if (p.slot >= sonificationSlot) { // 16 × 16: the mean colour
                    std::array<float, 3> rgb{};
                    const auto *px = reinterpret_cast<const unsigned char *>(p.pixels.constData());
                    for (int i = 0; i < 16 * 16 * 4; i += 4)
                        for (int c = 0; c < 3; ++c) rgb[size_t(c)] += px[i + c] / (255.f * 16 * 16);
                    listenedColors[p.slot - sonificationSlot] = rgb;
                    continue;
                }
                if (p.slot >= 0 && size_t(p.slot) < images.size()) { // a preview cell's picture, as pixels
                    images[size_t(p.slot)] = QImage(reinterpret_cast<const uchar *>(p.pixels.constData()), p.size.width(),
                                                    p.size.height(), p.size.width() * 4, QImage::Format_RGBA8888)
                                                 .copy();
                    ids[size_t(p.slot)] = p.id;
                }
            }
            {
                std::lock_guard<std::mutex> lk(m_previewMutex);
                m_previewImages = std::move(images);
                m_previewImageIds = ids;
                m_compositionImage = std::move(composition);
            }
            if (sourcePreviewDue && !sourcePreviewGiven) { // the layer has no picture: nothing to show
                std::lock_guard<std::mutex> lk(m_previewMutex);
                m_previewImage = QImage();
                m_previewImageId = m_previewId;
            }
            for (const auto &layer : m_layers) { // a sonification takes the colour of the viewport it listens to
                if (!layer->audio || !layer->audio->generator() || !layer->audio->generator()->sonification()) continue;
                const Layer *viewport = nullptr;
                for (const auto &candidate : m_layers) {
                    if (!candidate->isViewport) continue;
                    if (!viewport) viewport = candidate.get();
                    if (layer->sonificationViewport && candidate->id == layer->sonificationViewport) {
                        viewport = candidate.get();
                        break;
                    }
                }
                const quint64 id = viewport ? viewport->id : 0;
                for (size_t i = 0; i < listened.size(); ++i)
                    if (listened[i] == id)
                        if (const auto c = listenedColors.find(int(i)); c != listenedColors.end())
                            layer->audio->generator()->setColor(c->second[0], c->second[1], c->second[2]);
            }
            stepPixelMapping(); // the fixtures read the composition as it goes out
            m_published = m_back;
            m_back = 1 - m_back;
        }
        m_renderLayerIndexActive = false;
        m_mutex.unlock();
        m_renderLockHold.record(RealtimeTiming::nowNs() - lockAcquired);
        m_publishing.follow(publish); // the settings take effect at the next frame
        ++m_frameCount;
    }
}

void Engine::renderFrame()
{
    if (!m_initialized || m_threaded) return;
    const qint64 t0 = FrameTiming::nowNs();
    frame(nextDt());
    const qint64 t1 = FrameTiming::nowNs();
    const double rate = effectiveRender().frameRate;
    const bool presented = presentViewports(rate != RenderSettings::UncappedFrameRate);
    m_frameTiming.noteFrame(t0, t1, FrameTiming::nowNs(), rate > 0 ? qint64(1e9 / rate) : 0, presented);
    sampleResources();
    if (!m_framePending.exchange(true)) emit frameRendered();
}

QJsonObject Engine::frameTimingJson() const
{
    QJsonObject o = m_frameTiming.stats().json();
    auto realtime = [](const RealtimeTiming::Snapshot &s) {
        return QJsonObject{{"count", double(s.count)}, {"p99_ms", s.p99Ms()},
                           {"max_ms", double(s.maxNs) / 1e6},
                           {"over_quarter_budget", double(s.overQuarter)},
                           {"over_budget", double(s.overBudget)}};
    };
    o["audio_callback_lifetime"] = realtime(m_audio->callbackTiming());
    o["render_lock_wait_lifetime"] = realtime(m_renderLockWait.snapshot());
    o["render_lock_hold_lifetime"] = realtime(m_renderLockHold.snapshot());
    o["interaction_events_dropped_lifetime"] = double(m_interactionEventsDropped.load(std::memory_order_relaxed));
    // The pace the render keeps: the frame rate asked, else the output window's screen, else the main screen's
    const double rate = effectiveRender().frameRate;
    o["main_screen_hz"] = screenRefreshRate();
    // How late the cue clock fired the steps (after their pre-wait, or the GO of the step before)
    const Sequencer::CueTiming ct = m_sequencer->cueTiming();
    auto r2 = [](double ms) { return std::round(ms * 100.0) / 100.0; };
    o["cue_late"] = QJsonObject{{"count", double(ct.fired)}, {"last_ms", r2(ct.last * 1e3)}, {"max_ms", r2(ct.max * 1e3)},
                                {"mean_ms", r2(ct.fired ? ct.total / double(ct.fired) * 1e3 : 0)},
                                {"p99_ms", r2(ct.percentile(0.99) * 1e3)}};
    // Cue clock execution time (not timer scheduling lateness). These values
    // let operators distinguish a slow Undo capture from a slow source/fade
    // application, even when the clock itself fires exactly on time.
    const Sequencer::CueWorkTiming work = m_sequencer->cueWorkTiming();
    o["cue_capture"] = work.capture.json();
    o["cue_recall"] = work.recall.json();
    o["cue_execution"] = work.total.json();
    // Each output window: its screen's rate, the pictures shown, and those not shown by cause
    QJsonArray outs;
    double paceHz = rate > 0 ? rate : 0;
    {
        std::lock_guard<std::mutex> g(m_outputStatsMutex);
        for (const OutputStats &s : m_outputStats) {
            outs.append(QJsonObject{{"name", s.name},
                                    {"metal", s.metal},
                                    {"shown", s.exposed},
                                    {"screen_hz", s.hz},
                                    {"presented", double(s.presented)},
                                    {"no_drawable", double(s.noDrawable)},
                                    {"surfaces_busy", double(s.surfacesBusy)},
                                    {"failed", double(s.failed)},
                                    {"no_picture", double(s.noPicture)},
                                    {"not_render_thread", double(s.notRenderThread)}});
            if (rate == 0 && s.exposed && s.hz > 1) paceHz = s.hz; // the last one shown sets the pace
        }
    }
    o["outputs"] = outs;
    o["pace_hz"] = rate == RenderSettings::UncappedFrameRate ? 0 : (paceHz > 0 ? paceHz : screenRefreshRate());
    o["outputs_dropped"] = double(m_outputsDropped.load());
    o["gpu_shares_skipped"] = double(m_sharesSkipped.load());          // Syphon / Spout, one frame later: not ready
    o["readbacks_skipped"] = double(m_publishing.readbacksSkipped()); // NDI / OMT: the GPU not done in time
    return o;
}

void Engine::resetFrameTiming()
{
    m_frameTiming.reset();
    m_sequencer->resetCueTiming(); // the OSC timing reset must reset every reported cue histogram too
    m_outputsDropped = 0;
    m_sharesSkipped = 0;
}

// Last finished picture of one viewport (what its window and its publishers get)
QImage Engine::grabViewport(quint64 viewport, bool deep)
{
    QImage img;
    runGl([this, viewport, deep, &img] {
        if (m_rhi) img = m_rhi->grabViewport(viewport, deep);
    });
    return img;
}

QImage Engine::grabOutput()
{
    QImage img;
    runGl([this, &img] {
        if (m_rhi) img = m_rhi->grabOutput();
    });
    return img;
}

QImage Engine::grabLayerSource(int index, bool deep)
{
    QImage img;
    quint64 id = 0;
    {
        Lock lk(&m_mutex);
        if (const Layer *l = layer(index)) id = l->id;
    }
    runGl([this, id, deep, &img] {
        if (m_rhi) img = m_rhi->grabSource(id, deep);
    });
    return img;
}

const void *Engine::layerPicture(quint64 id, LayerPicture which, QSize *size)
{
    const void *picture = nullptr;
    QSize s;
    runGl([&] {
        if (m_rhi) picture = m_rhi->picture(id, int(which), &s);
    });
    if (size) *size = s;
    return picture;
}

QSize Engine::videoPictureSize(int index)
{
    quint64 id = 0;
    {
        Lock lk(&m_mutex);
        if (const Layer *l = layer(index)) id = l->id;
    }
    QSize s;
    runGl([&] {
        if (m_rhi) s = m_rhi->videoSize(id);
    });
    return s;
}

quint64 Engine::videoFramesShown(int index) const
{
    quint64 id = 0;
    {
        Lock lk(&m_mutex);
        const Layer *l = index >= 0 && index < int(m_layers.size()) ? m_layers[size_t(index)].get() : nullptr;
        if (l) id = l->id;
    }
    quint64 n = 0;
    const_cast<Engine *>(this)->runGl([this, id, &n] {
        if (m_rhi) n = m_rhi->videoFramesShown(id);
    });
    return n;
}

// ---------------------------------------------------------------------------
// Publishing (NDI, OMT, Syphon, Spout)
// ---------------------------------------------------------------------------
void Engine::setPublishSettings(quint64 viewport, const PublishSettings &s)
{
    Lock lk(&m_mutex);
    for (auto &l : m_layers)
        if (l->isViewport && l->id == viewport) l->vpPublish = s;
}

PublishSettings Engine::publishSettings(quint64 viewport) const
{
    Lock lk(&m_mutex);
    for (const auto &l : m_layers)
        if (l->isViewport && l->id == viewport) return l->vpPublish;
    return {};
}

void Engine::setTestTap(std::function<void(const CpuFrame &)> fn) { m_publishing.setTap(std::move(fn)); }
void Engine::setGpuPublisherFactoryForTests(std::function<std::unique_ptr<GpuPublisher>(PublishKind)> f)
{
    m_publishing.setGpuPublisherFactoryForTests(std::move(f));
}

PublishState Engine::publishState(quint64 viewport, PublishKind k) const { return m_publishing.state(viewport, k); }
