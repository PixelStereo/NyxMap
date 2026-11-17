#include "PreviewWorkspace.h"
#include "MappingView.h"
#include "Scene3DView.h"
#include "Engine.h"
#include "PaintOverlay.h"
#include "RhiDraw.h"
#include "FixtureOverlay.h"
#include "ShowMenus.h"
#include "Widgets.h"
#include "Commands.h"
#include <QUndoStack>
#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QGridLayout>
#include <QJsonArray>
#include <QMenu>
#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <QPainter>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

// A layer, a viewport or the composition as rendered, with the bar every preview has: zoom (buttons, wheel or
// pinch, around the cursor), Fit, and the shared magnet / auto-select switches. Pan: middle button, Alt/⌥ + drag,
// or two fingers on a trackpad.
class PreviewPicture : public QRhiWidget
{
public:
    PreviewPicture(Engine *engine, int slot) : m_engine(engine), m_slot(slot)
    {
        setMinimumSize(100, 70);
        setMouseTracking(true);
        m_overlay = new PaintOverlay(this, [this](QPainter &p) {
            if (!m_source) paintViewportFrames(p); // the composition: where each viewport looks
            else if (m_maskMode) {
                paintMasks(p); // the masks of what is shown
            } else {
                paintWarpHandles(p); // a viewport: the points of its warp, then the ramps of its soft edge, to drag
                paintEdgeHandles(p);
            }
            if (fixturesShown()) paintFixtures(p);
        }); // under the bar
        m_bar = new ZoomBar(this, QStringLiteral("Whole picture in view\n"
                                                 "Pan: middle button, Alt/⌥ + drag, or two fingers on a trackpad"));
        m_bar->setObjectName(QStringLiteral("previewZoomBar%1").arg(slot));
        connect(m_bar, &ZoomBar::zoomOutClicked, this, [this] { zoomAt(QPointF(width() / 2.0, height() / 2.0), 1 / 1.25); });
        connect(m_bar, &ZoomBar::zoomInClicked, this, [this] { zoomAt(QPointF(width() / 2.0, height() / 2.0), 1.25); });
        connect(m_bar, &ZoomBar::fitClicked, this, [this] { fit(); });
        // Each view chooses whether it shows the pixel mapping fixtures (the composition's or a viewport's picture)
        m_fixturesButton = m_bar->addButton(QStringLiteral("Fixtures"),
                                            QStringLiteral("Show the pixel mapping fixtures over this view"));
        m_fixturesButton->setObjectName(QStringLiteral("previewFixtures%1").arg(slot));
        m_fixturesButton->setCheckable(true);
        m_fixturesButton->setChecked(QSettings().value(settingsKey(), false).toBool());
        connect(m_fixturesButton, &QToolButton::toggled, this, [this](bool on) {
            QSettings().setValue(settingsKey(), on);
            update();
        });
        connect(engine, &Engine::fixturesChanged, this, qOverload<>(&QWidget::update));
        // The same two tools as the composition's view: Layer (the view only shows) and Mask (the masks of what is shown are
        // edited here: drag a point or a handle, a shape whole; a click on an empty place draws a new one)
        m_maskButton = m_bar->addButton(QStringLiteral("Layer"), QStringLiteral("Layer mode: the view only shows. Press to switch to Mask mode.\n"
                                                                            "In Mask mode: drag a point, a handle or a shape; click an empty place to draw a new mask "
                                                                            "(click the first point, double-click or Enter to close it; Esc cancels); Delete removes a point."));
        m_maskButton->setObjectName(QStringLiteral("previewSelectionMode"));
        m_maskButton->setCheckable(true);
        connect(m_maskButton, &QToolButton::toggled, this, [this](bool on) { setMaskMode(on); });
        m_channelButton = m_bar->addChannelButton([this](int channel) {
            m_channel = channel;
            update();
        });
    }
    // The source shown (0: the composition): fixtures are drawn over the composition and the viewports only
    void setSource(quint64 id)
    {
        clearProbe();
        m_source = id;
        m_fixturesButton->setVisible(showsComposition());
        m_maskButton->setVisible(id != 0);
        if (!id) setMaskMode(false);
        m_maskSel = m_nodeSel = -1;
        m_drawing = false;
        m_drawNodes.clear();
        update();
    }
    bool fixturesShown() const { return m_fixturesButton->isChecked() && showsComposition(); }
    void fit()
    {
        m_zoom = 1.0;
        m_pan = QPointF();
        m_bar->setZoom(m_zoom);
        update();
    }
    double zoom() const { return m_zoom; }
    ~PreviewPicture() override { clearProbe(); }
    // A viewport's soft edge is dragged here: its change is one step of the undo stack, and the inspector is told
    void setUndoStack(QUndoStack *undo) { m_undo = undo; }
    std::function<void()> onEdited;
    std::function<void()> onMasksEdited;
    std::function<void(int)> onMaskPicked;
    // The mask picked elsewhere (the inspector's list) is the one drawn selected here
    void selectMask(int index)
    {
        m_maskSel = index;
        m_nodeSel = -1;
        m_overlay->update();
    }
    // The cross on the output of the viewport shown, where the pointer is (the workspace's switch)
    void setCross(bool on)
    {
        m_cross = on;
        if (!on) clearProbe();
    }
    void clearProbe()
    {
        if (!m_probed) return;
        m_engine->setViewportProbe(m_probed, QPointF(-1, -1));
        m_probed = 0;
    }
    bool inViewport() const
    {
        QSize size;
        Engine::Lock lock(&m_engine->mutex());
        return viewportShown(&size) != nullptr;
    }
    QJsonObject viewState() const
    {
        return {{"zoom", m_zoom}, {"pan", QJsonArray{m_pan.x(), m_pan.y()}}, {"fixtures", m_fixturesButton->isChecked()}, {"channel", m_channel}};
    }
    void setViewState(const QJsonObject &o)
    {
        if (o.contains("zoom")) m_zoom = std::clamp(o.value("zoom").toDouble(1.0), 0.25, 64.0);
        const QJsonArray pan = o.value("pan").toArray();
        if (pan.size() == 2) m_pan = QPointF(pan[0].toDouble(), pan[1].toDouble());
        if (o.contains("fixtures")) m_fixturesButton->setChecked(o.value("fixtures").toBool());
        m_channel = std::clamp(o.value("channel").toInt(0), 0, 4);
        ZoomBar::showChannel(m_channelButton, m_channel);
        m_bar->setZoom(m_zoom);
        update();
    }

protected:
    void initialize(QRhiCommandBuffer *) override { m_draw.initialize(rhi(), renderTarget()); }
    void releaseResources() override { m_draw.release(); }
    void resizeEvent(QResizeEvent *e) override
    {
        QRhiWidget::resizeEvent(e);
        m_overlay->follow();
    }
    void render(QRhiCommandBuffer *cb) override
    {
        m_draw.initialize(rhi(), renderTarget());
        m_draw.begin(QColor::fromRgbF(0.07f, 0.07f, 0.08f));
        const QImage image = m_engine->previewImage(m_slot);
        if (!image.isNull()) {
            if (image.cacheKey() != m_pictureKey) {
                m_draw.setPicture(image);
                m_pictureKey = image.cacheKey();
            }
            m_picture = image.size();
            const QRectF r = viewRect();
            const float w = float(std::max(1, width())), h = float(std::max(1, height()));
            m_draw.setChannel(m_channel);
            m_draw.drawPicture(float(r.left() / w * 2 - 1), float(1 - r.bottom() / h * 2), float(r.right() / w * 2 - 1),
                               float(1 - r.top() / h * 2));
        }
        m_draw.render(cb, renderTarget());
        m_overlay->update();
    }
    // ---- Masks (Masks.h): those of the layer or viewport shown, in the frame of its own picture -----------------------------
    void setMaskMode(bool on)
    {
        if (m_maskMode == on) return;
        m_maskMode = on;
        m_maskDrag = Drag::None;
        m_drawing = false;
        m_drawNodes.clear();
        m_maskSel = m_nodeSel = -1;
        m_hover = m_warpHover = -1;
        QSignalBlocker block(m_maskButton);
        m_maskButton->setChecked(on);
        m_maskButton->setText(on ? QStringLiteral("Mask") : QStringLiteral("Layer"));
        setFocusPolicy(on ? Qt::StrongFocus : Qt::NoFocus);
        setCursor(Qt::ArrowCursor);
        m_overlay->update();
        update();
    }
    QPointF maskToWidget(QPointF n) const
    {
        const QRectF r = viewRect();
        return QPointF(r.left() + n.x() * r.width(), r.top() + n.y() * r.height());
    }
    QPointF widgetToMask(QPointF w) const
    {
        const QRectF r = viewRect();
        return QPointF((w.x() - r.left()) / std::max(1.0, r.width()), (w.y() - r.top()) / std::max(1.0, r.height()));
    }
    // The shape of a mask on the widget, its curves flattened
    QPolygonF maskPolygon(const Mask &k) const
    {
        QPolygonF out;
        if (!k.valid()) return out;
        const double scale = 1000;
        for (const QPolygonF &poly : k.path(QSizeF(scale, scale)).toSubpathPolygons())
            for (const QPointF &q : poly) out << maskToWidget(q / scale);
        return out;
    }
    void paintMasks(QPainter &p)
    {
        if (!m_source) return;
        const std::vector<Mask> list = m_engine->layerMasks(m_source);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < int(list.size()); ++i) {
            const Mask &k = list[size_t(i)];
            if (k.attach != Mask::Attach::Surface || !k.valid()) continue;
            const bool picked = i == m_maskSel;
            p.setPen(QPen(picked ? QColor(255, 210, 60) : QColor(120, 190, 255, k.enabled ? 220 : 90), picked ? 2.0 : 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(maskPolygon(k));
            if (!picked) continue;
            for (int j = 0; j < int(k.nodes.size()); ++j) {
                const MaskNode &n = k.nodes[size_t(j)];
                const QPointF c = maskToWidget(n.p);
                if (j == m_nodeSel) { // its handles
                    p.setPen(QPen(QColor(255, 210, 60), 1));
                    for (const QPointF &h : {n.in, n.out})
                        if (h != n.p) {
                            p.drawLine(c, maskToWidget(h));
                            p.setBrush(QColor(255, 210, 60));
                            p.drawEllipse(maskToWidget(h), 3.5, 3.5);
                        }
                }
                p.setPen(QPen(Qt::black, 1));
                p.setBrush(j == m_nodeSel ? QColor(255, 255, 255) : QColor(255, 210, 60));
                p.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
            }
        }
        if (m_drawing && !m_drawNodes.empty()) { // the mask being drawn
            p.setPen(QPen(QColor(255, 255, 255), 1.2, Qt::DashLine));
            QPolygonF line;
            for (const MaskNode &n : m_drawNodes) line << maskToWidget(n.p);
            line << m_drawCursor;
            p.drawPolyline(line);
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(QColor(255, 255, 255));
            for (const MaskNode &n : m_drawNodes) p.drawRect(QRectF(maskToWidget(n.p).x() - 4, maskToWidget(n.p).y() - 4, 8, 8));
        }
        p.restore();
    }
    static bool nearPoint(QPointF a, QPointF b, double radius) { return QLineF(a, b).length() <= radius; }
    // A mask edit is one undo step: the masks before, then after
    void commitMasks(const std::vector<Mask> &after, const QString &text)
    {
        if (m_undo && after != m_maskBefore) m_undo->push(new cmd::SetMasks(m_engine, m_source, m_maskBefore, after, text));
        else m_engine->setLayerMasks(m_source, after);
        if (onMasksEdited) onMasksEdited();
        m_overlay->update();
    }
    bool pressMask(QMouseEvent *e)
    {
        if (!m_source || m_engine->isLocked(m_engine->indexOfId(m_source))) return false;
        const QPointF at = e->position();
        m_maskBefore = m_engine->layerMasks(m_source);
        if (m_drawing) {
            if (m_drawNodes.size() >= 3 && nearPoint(maskToWidget(m_drawNodes.front().p), at, 8)) {
                finishDrawing();
                return true;
            }
            const QPointF q = widgetToMask(at);
            m_drawNodes.push_back({q, q, q});
            m_overlay->update();
            return true;
        }
        // a handle or point of the mask picked, then another one's point, then the inside of a shape
        if (m_maskSel >= 0 && m_maskSel < int(m_maskBefore.size())) {
            const Mask &k = m_maskBefore[size_t(m_maskSel)];
            if (m_nodeSel >= 0 && m_nodeSel < int(k.nodes.size())) {
                const MaskNode &n = k.nodes[size_t(m_nodeSel)];
                if (n.out != n.p && nearPoint(maskToWidget(n.out), at, 6)) return beginDrag(Drag::Out, at);
                if (n.in != n.p && nearPoint(maskToWidget(n.in), at, 6)) return beginDrag(Drag::In, at);
            }
            for (int j = 0; j < int(k.nodes.size()); ++j)
                if (nearPoint(maskToWidget(k.nodes[size_t(j)].p), at, 7)) {
                    m_nodeSel = j;
                    return beginDrag(Drag::Node, at);
                }
        }
        for (int i = int(m_maskBefore.size()) - 1; i >= 0; --i) {
            const Mask &k = m_maskBefore[size_t(i)];
            if (k.attach != Mask::Attach::Surface || !k.valid()) continue;
            for (int j = 0; j < int(k.nodes.size()); ++j)
                if (nearPoint(maskToWidget(k.nodes[size_t(j)].p), at, 7)) {
                    m_maskSel = i;
                    m_nodeSel = j;
                    if (onMaskPicked) onMaskPicked(i);
                    return beginDrag(Drag::Node, at);
                }
            if (maskPolygon(k).containsPoint(at, Qt::OddEvenFill)) {
                m_maskSel = i;
                m_nodeSel = -1;
                if (onMaskPicked) onMaskPicked(i);
                return beginDrag(Drag::Whole, at);
            }
        }
        // an empty place: a new mask begins
        m_maskSel = m_nodeSel = -1;
        const QPointF q = widgetToMask(at);
        m_drawing = true;
        m_drawNodes.assign(1, MaskNode{q, q, q});
        m_drawCursor = at;
        m_overlay->update();
        return true;
    }
    bool beginDrag(int kind, QPointF at)
    {
        m_maskDrag = kind;
        m_dragFrom = widgetToMask(at);
        m_overlay->update();
        return true;
    }
    void dragMask(QPointF at)
    {
        if (m_maskDrag == Drag::None || m_maskSel < 0 || m_maskSel >= int(m_maskBefore.size())) return;
        std::vector<Mask> now = m_maskBefore;
        Mask &k = now[size_t(m_maskSel)];
        const QPointF q = widgetToMask(at), d = q - m_dragFrom;
        if (m_maskDrag == Drag::Whole) {
            k.translate(d);
        } else if (m_nodeSel >= 0 && m_nodeSel < int(k.nodes.size())) {
            MaskNode &n = k.nodes[size_t(m_nodeSel)];
            if (m_maskDrag == Drag::Node) {
                n.p += d;
                n.in += d;
                n.out += d;
            } else if (m_maskDrag == Drag::Out) {
                n.out = q;
            } else {
                n.in = q;
            }
        }
        m_engine->setLayerMasks(m_source, now); // live; one undo step at the release
        m_overlay->update();
        if (onMasksEdited) onMasksEdited();
    }
    void finishDrawing()
    {
        if (m_drawNodes.size() >= 3) {
            std::vector<Mask> after = m_engine->layerMasks(m_source);
            m_maskBefore = after;
            Mask k;
            k.name = masks::uniqueName(after);
            k.nodes = m_drawNodes;
            after.push_back(k);
            m_maskSel = int(after.size()) - 1;
            m_nodeSel = -1;
            commitMasks(after, QStringLiteral("Draw Mask"));
            if (onMaskPicked) onMaskPicked(m_maskSel);
        }
        m_drawing = false;
        m_drawNodes.clear();
        m_overlay->update();
    }
    void releaseMask()
    {
        if (m_maskDrag == Drag::None) return;
        m_maskDrag = Drag::None;
        const std::vector<Mask> after = m_engine->layerMasks(m_source);
        m_engine->setLayerMasks(m_source, m_maskBefore); // the masks as they were, then the change as one step
        commitMasks(after, QStringLiteral("Edit Mask"));
    }
    // The composition: the frame of each viewport, with its name (the mapping view draws them too, to edit them)
    void paintViewportFrames(QPainter &p)
    {
        Engine::Lock lock(&m_engine->mutex());
        const QRectF r = viewRect();
        auto toWidget = [&](QPointF n) { return QPointF(r.left() + n.x() * r.width(), r.top() + n.y() * r.height()); };
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        const QColor color(120, 190, 255, 230);
        p.setPen(QPen(color, 1.5));
        p.setBrush(Qt::NoBrush);
        for (int i : m_engine->viewports()) {
            const Layer *v = m_engine->layer(i);
            if (!v || !v->enabled) continue;
            QPolygonF frame;
            for (int k = 0; k < 4; ++k) frame << toWidget(v->mapping.corner(k));
            p.drawPolygon(frame);
            p.drawText(frame.first() + QPointF(5, 14), v->name);
        }
        p.restore();
    }
    // The viewport shown (null: not a viewport), its size in pixels
    const Layer *viewportShown(QSize *size) const
    {
        const int index = m_source ? m_engine->indexOfId(m_source) : -1;
        const Layer *l = index >= 0 ? m_engine->layer(index) : nullptr;
        if (!l || !l->isViewport) return nullptr;
        *size = l->viewportSize();
        return size->isEmpty() ? nullptr : l;
    }
    // Where the ramp of a side ends in the widget (pixels of the output -> widget), and its grip: a small bar on the line
    struct Handle {
        QLineF line;
        QRectF grip;
        QRectF band;
    };
    Handle handleOf(int side, double width, const QRectF &r, double scale) const
    {
        const double d = width * scale;
        Handle h;
        switch (side) {
        case 0: h.line = QLineF(r.left() + d, r.top(), r.left() + d, r.bottom()); h.band = QRectF(r.left(), r.top(), d, r.height()); break;
        case 1: h.line = QLineF(r.right() - d, r.top(), r.right() - d, r.bottom()); h.band = QRectF(r.right() - d, r.top(), d, r.height()); break;
        case 2: h.line = QLineF(r.left(), r.top() + d, r.right(), r.top() + d); h.band = QRectF(r.left(), r.top(), r.width(), d); break;
        default: h.line = QLineF(r.left(), r.bottom() - d, r.right(), r.bottom() - d); h.band = QRectF(r.left(), r.bottom() - d, r.width(), d); break;
        }
        const QPointF c = h.line.pointAt(0.5);
        h.grip = side < 2 ? QRectF(c.x() - 4, c.y() - 16, 8, 32) : QRectF(c.x() - 16, c.y() - 4, 32, 8);
        return h;
    }
    void paintEdgeHandles(QPainter &p)
    {
        QSize size;
        double widths[4];
        {
            Engine::Lock lock(&m_engine->mutex());
            const Layer *v = viewportShown(&size);
            if (!v || m_engine->isLocked(m_engine->indexOfId(m_source))) return;
            for (int k = 0; k < 4; ++k) widths[k] = m_dragging == k ? m_dragWidth : v->softWidth[k];
        }
        const QRectF r = viewRect();
        const double scale = r.width() / size.width();
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        const QColor accent(255, 170, 40);
        for (int k = 0; k < 4; ++k) {
            const Handle h = handleOf(k, widths[k], r, scale);
            if (widths[k] > 0) {
                p.fillRect(h.band, QColor(accent.red(), accent.green(), accent.blue(), 55));
                p.setPen(QPen(accent, 1, Qt::DashLine));
                p.drawLine(h.line);
            }
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(m_dragging == k || m_hover == k ? accent : QColor(accent.red(), accent.green(), accent.blue(), 200));
            p.drawRoundedRect(h.grip, 3, 3);
            if (m_dragging == k || m_hover == k) {
                p.setPen(Qt::white);
                const QString text = QStringLiteral("%1 px").arg(qRound(widths[k]));
                const QRectF box(h.grip.center() + QPointF(k == 1 ? -70 : 10, k == 2 ? 6 : k == 3 ? -22 : -9), QSizeF(60, 16));
                p.drawText(box, k == 1 ? Qt::AlignRight : Qt::AlignLeft, text);
            }
        }
        p.restore();
    }
    int gripAt(QPointF at) const
    {
        QSize size;
        double widths[4];
        {
            Engine::Lock lock(&m_engine->mutex());
            const Layer *v = viewportShown(&size);
            if (!v || m_engine->isLocked(m_engine->indexOfId(m_source))) return -1;
            for (int k = 0; k < 4; ++k) widths[k] = v->softWidth[k];
        }
        const QRectF r = viewRect();
        for (int k = 0; k < 4; ++k)
            if (handleOf(k, widths[k], r, r.width() / size.width()).grip.adjusted(-4, -4, 4, 4).contains(at)) return k;
        return -1;
    }
    // The warp's points in the widget: its four corners, or the points of its mesh (the picture is the output: a point's
    // position on the output is its place in the picture)
    struct WarpPoint {
        QPointF at;
        int corner = -1, i = 0, j = 0;
    };
    std::vector<WarpPoint> warpPoints(const Layer &v, const QRectF &r, int *cols = nullptr, int *rows = nullptr) const
    {
        std::vector<WarpPoint> out;
        auto toWidget = [&](QPointF n) { return QPointF(r.left() + n.x() * r.width(), r.top() + n.y() * r.height()); };
        if (v.warpEdit == 1) {
            for (int k = 0; k < 4; ++k) out.push_back({toWidget(v.warp.corner(k)), k, 0, 0});
        } else if (v.warpEdit == 2) {
            if (cols) *cols = v.warp.cols;
            if (rows) *rows = v.warp.rows;
            for (int j = 0; j < v.warp.rows; ++j)
                for (int i = 0; i < v.warp.cols; ++i) out.push_back({toWidget(v.warp.controlPoint(i, j)), -1, i, j});
        }
        return out;
    }
    void paintWarpHandles(QPainter &p)
    {
        std::vector<WarpPoint> points;
        int cols = 0, rows = 0;
        {
            Engine::Lock lock(&m_engine->mutex());
            QSize size;
            const Layer *v = viewportShown(&size);
            if (!v || m_engine->isLocked(m_engine->indexOfId(m_source))) return;
            points = warpPoints(*v, viewRect(), &cols, &rows);
        }
        if (points.empty()) return;
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        const QColor line(80, 200, 255);
        if (cols > 1 && rows > 1) { // the mesh's lines
            p.setPen(QPen(QColor(line.red(), line.green(), line.blue(), 170), 1));
            for (int j = 0; j < rows; ++j)
                for (int i = 0; i < cols; ++i) {
                    const QPointF a = points[size_t(j) * cols + i].at;
                    if (i + 1 < cols) p.drawLine(a, points[size_t(j) * cols + i + 1].at);
                    if (j + 1 < rows) p.drawLine(a, points[size_t(j + 1) * cols + i].at);
                }
        } else if (points.size() == 4) { // the corners' quad
            p.setPen(QPen(QColor(line.red(), line.green(), line.blue(), 170), 1));
            for (int k = 0; k < 4; ++k) p.drawLine(points[size_t(k)].at, points[size_t((k + 1) % 4)].at);
        }
        for (size_t k = 0; k < points.size(); ++k) {
            const bool hot = m_warpDrag ? int(k) == m_warpDragIndex : int(k) == m_warpHover;
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(hot ? QColor(255, 255, 255) : line);
            const double h = hot ? 6 : 5;
            p.drawRect(QRectF(points[k].at.x() - h, points[k].at.y() - h, 2 * h, 2 * h));
        }
        p.restore();
    }
    // The warp point under `at` (its index), or -1
    int warpPointAt(QPointF at) const
    {
        Engine::Lock lock(&m_engine->mutex());
        QSize size;
        const Layer *v = viewportShown(&size);
        if (!v || m_engine->isLocked(m_engine->indexOfId(m_source))) return -1;
        const std::vector<WarpPoint> points = warpPoints(*v, viewRect());
        int best = -1;
        double nearest = 10 * 10; // within 10 pixels
        for (size_t k = 0; k < points.size(); ++k) {
            const QPointF d = points[k].at - at;
            const double q = d.x() * d.x() + d.y() * d.y();
            if (q <= nearest) {
                nearest = q;
                best = int(k);
            }
        }
        return best;
    }
    void dragWarpPoint(QPointF at)
    {
        const QRectF r = viewRect();
        const QPointF n(std::clamp((at.x() - r.left()) / r.width(), -0.5, 1.5), std::clamp((at.y() - r.top()) / r.height(), -0.5, 1.5));
        {
            Engine::Lock lock(&m_engine->mutex());
            Layer *l = m_engine->layer(m_engine->indexOfId(m_source));
            if (!l || !l->isViewport) return;
            if (l->warpEdit == 1) l->warp.setCorner(m_warpDragIndex, n);
            else if (l->warpEdit == 2) l->warp.setControlPoint(m_warpDragIndex % l->warp.cols, m_warpDragIndex / l->warp.cols, n);
        }
        update();
        m_overlay->update();
        if (onEdited) onEdited();
    }
    // The cross on the output follows the pointer over the picture
    void followPointer(QPointF at)
    {
        if (!m_cross || !m_source || !inViewport()) return;
        const QRectF r = viewRect();
        const QPointF n((at.x() - r.left()) / r.width(), (at.y() - r.top()) / r.height());
        if (n.x() < 0 || n.y() < 0 || n.x() > 1 || n.y() > 1) return clearProbe();
        m_probed = m_source;
        m_engine->setViewportProbe(m_source, n);
    }
    static QString softPath(int side)
    {
        static const char *const kSides[] = {"left", "right", "top", "bottom"};
        return QStringLiteral("soft_edge/%1/width").arg(QLatin1String(kSides[side]));
    }
    void dragEdge(QPointF at)
    {
        QSize size;
        {
            Engine::Lock lock(&m_engine->mutex());
            if (!viewportShown(&size)) return;
        }
        const QRectF r = viewRect();
        const double scale = r.width() / size.width();
        const double from = m_dragging == 0 ? at.x() - r.left()
                            : m_dragging == 1 ? r.right() - at.x()
                            : m_dragging == 2 ? at.y() - r.top()
                                              : r.bottom() - at.y();
        const double limit = m_dragging < 2 ? size.width() : size.height();
        m_dragWidth = std::round(std::clamp(from / scale, 0.0, limit));
        m_engine->setParameter(m_source, softPath(m_dragging), m_dragWidth); // live; one undo step at the release
        update();
        m_overlay->update();
        if (onEdited) onEdited();
    }
    // Over the picture, with a QPainter
    void paintFixtures(QPainter &p)
    {
        Engine::Lock lock(&m_engine->mutex());
        const FixturePatch patch = m_engine->fixturePatch();
        if (patch.fixtures.empty()) return;
        const int index = m_source ? m_engine->indexOfId(m_source) : -1;
        const Layer *viewport = index >= 0 ? m_engine->layer(index) : nullptr;
        const QRectF r = viewRect();
        // A composition point in the picture: through the viewport's frame when the picture is a viewport's
        auto toWidget = [&](QPointF n) {
            const QPointF uv = viewport ? viewport->mapping.unmap(n, n) : n;
            return QPointF(r.left() + uv.x() * r.width(), r.top() + uv.y() * r.height());
        };
        fixtureview::paint(p, patch, m_engine->compositionSize(), toWidget, m_engine->fixtureLooks());
    }
    void wheelEvent(QWheelEvent *e) override
    {
        // As in the composition's view: trackpad scroll pans, a pinch or Ctrl/⌘ + scroll or the mouse wheel zooms
        const bool trackpad = e->phase() != Qt::NoScrollPhase && !e->pixelDelta().isNull();
        if (trackpad && !(e->modifiers() & Qt::ControlModifier)) {
            m_pan += QPointF(e->pixelDelta());
            update();
        } else {
            const double steps = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->pixelDelta().y() * 2.0;
            zoomAt(e->position(), std::pow(1.0015, steps));
        }
        e->accept();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (m_maskMode && e->button() == Qt::LeftButton && !(e->modifiers() & Qt::AltModifier) && pressMask(e)) {
            setFocus();
            e->accept();
            return;
        }
        if (m_maskMode && e->button() != Qt::MiddleButton && !(e->modifiers() & Qt::AltModifier)) {
            e->accept();
            return;
        }
        if (e->button() == Qt::LeftButton && !(e->modifiers() & Qt::AltModifier)) {
            if (const int w = warpPointAt(e->position()); w >= 0) {
                m_warpDrag = true;
                m_warpDragIndex = w;
                m_warpBefore = cmd::SetMapping::read(m_engine, m_engine->indexOfId(m_source), true);
                e->accept();
                return;
            }
            if (const int k = gripAt(e->position()); k >= 0) {
                m_dragging = k;
                {
                    Engine::Lock lock(&m_engine->mutex());
                    m_dragStart = m_engine->parameterValue(m_source, softPath(k)).toDouble();
                }
                m_dragWidth = m_dragStart;
                e->accept();
                return;
            }
        }
        if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier))) {
            m_panning = true;
            m_panLast = e->position();
            e->accept();
            return;
        }
        QRhiWidget::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_maskMode) {
            if (m_maskDrag != Drag::None) dragMask(e->position());
            else if (m_drawing) {
                m_drawCursor = e->position();
                m_overlay->update();
            }
            if (!m_panning) {
                e->accept();
                return;
            }
        }
        if (m_warpDrag) {
            dragWarpPoint(e->position());
            e->accept();
            return;
        }
        if (m_dragging >= 0) {
            dragEdge(e->position());
            e->accept();
            return;
        }
        if (!m_panning) {
            followPointer(e->position());
            const int w = m_source ? warpPointAt(e->position()) : -1;
            const int k = m_source && w < 0 ? gripAt(e->position()) : -1;
            if (k != m_hover || w != m_warpHover) {
                m_hover = k;
                m_warpHover = w;
                setCursor(w >= 0 ? Qt::CrossCursor : k < 0 ? Qt::ArrowCursor : k < 2 ? Qt::SizeHorCursor : Qt::SizeVerCursor);
                m_overlay->update();
            }
            return QRhiWidget::mouseMoveEvent(e);
        }
        m_pan += e->position() - m_panLast;
        m_panLast = e->position();
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (m_maskDrag != Drag::None) {
            releaseMask();
            e->accept();
            return;
        }
        if (m_warpDrag) { // the point is where it was dropped: the move is one step
            m_warpDrag = false;
            const int index = m_engine->indexOfId(m_source);
            const Mapping after = cmd::SetMapping::read(m_engine, index, true);
            if (m_undo && after != m_warpBefore)
                m_undo->push(new cmd::SetMapping(m_engine, index, m_warpBefore, after, QStringLiteral("Move Warp Point"), false, true));
            if (onEdited) onEdited();
            m_overlay->update();
            e->accept();
            return;
        }
        if (m_dragging >= 0) { // the width as it was before, then the change as one step
            const int side = m_dragging;
            m_dragging = -1;
            m_engine->setParameter(m_source, softPath(side), m_dragStart);
            if (m_undo) cmd::setValue(m_undo, m_engine, m_source, softPath(side), m_dragWidth, QStringLiteral("Soft Edge"));
            else m_engine->setParameter(m_source, softPath(side), m_dragWidth);
            if (onEdited) onEdited();
            m_overlay->update();
            e->accept();
            return;
        }
        if (!m_panning) return QRhiWidget::mouseReleaseEvent(e);
        m_panning = false;
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (m_maskMode && m_drawing) {
            if (!m_drawNodes.empty()) m_drawNodes.pop_back(); // the second click of the double-click added a point
            finishDrawing();
            e->accept();
            return;
        }
        if (!m_maskMode) fit();
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (!m_maskMode) return QRhiWidget::keyPressEvent(e);
        if (e->key() == Qt::Key_Escape) {
            m_drawing = false;
            m_drawNodes.clear();
            m_maskSel = m_nodeSel = -1;
            m_overlay->update();
        } else if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && m_drawing) {
            finishDrawing();
        } else if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && m_maskSel >= 0 && m_nodeSel >= 0) {
            std::vector<Mask> after = m_engine->layerMasks(m_source);
            m_maskBefore = after;
            if (m_maskSel < int(after.size()) && after[size_t(m_maskSel)].nodes.size() > 3) {
                after[size_t(m_maskSel)].nodes.erase(after[size_t(m_maskSel)].nodes.begin() + m_nodeSel);
                m_nodeSel = -1;
                commitMasks(after, QStringLiteral("Delete Mask Point"));
            }
        } else {
            return QRhiWidget::keyPressEvent(e);
        }
        e->accept();
    }
    void leaveEvent(QEvent *e) override
    {
        clearProbe();
        QRhiWidget::leaveEvent(e);
    }

private:
    QRectF fitRect() const
    {
        QSizeF s = m_picture.isEmpty() ? QSizeF(size()) : QSizeF(m_picture);
        s.scale(QSizeF(size()), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    }
    QRectF viewRect() const
    {
        const QRectF fit = fitRect();
        QRectF r(0, 0, fit.width() * m_zoom, fit.height() * m_zoom);
        r.moveCenter(fit.center() + m_pan);
        return r;
    }
    void zoomAt(QPointF at, double factor)
    {
        const QRectF before = viewRect();
        const QPointF n((at.x() - before.left()) / std::max(1.0, before.width()),
                        (at.y() - before.top()) / std::max(1.0, before.height()));
        m_zoom = std::clamp(m_zoom * factor, 0.25, 64.0);
        const QRectF fit = fitRect();
        const double w = fit.width() * m_zoom, h = fit.height() * m_zoom;
        m_pan = QPointF(at.x() - n.x() * w + w / 2, at.y() - n.y() * h + h / 2) - fit.center();
        m_bar->setZoom(m_zoom);
        update();
    }

    bool showsComposition() const
    {
        if (!m_source) return true;
        Engine::Lock lock(&m_engine->mutex());
        const int index = m_engine->indexOfId(m_source);
        return index >= 0 && m_engine->isViewport(index);
    }
    QString settingsKey() const { return QStringLiteral("view/fixtures/preview%1").arg(m_slot); }

    Engine *m_engine;
    int m_slot;
    quint64 m_source = 0;
    QToolButton *m_fixturesButton = nullptr, *m_channelButton = nullptr;
    int m_channel = 0; // RGB, R, G, B, A
    RhiDraw m_draw;
    qint64 m_pictureKey = 0;
    PaintOverlay *m_overlay = nullptr;
    ZoomBar *m_bar = nullptr;
    QSize m_picture;
    double m_zoom = 1.0;
    QPointF m_pan, m_panLast;
    bool m_panning = false;
    int m_dragging = -1, m_hover = -1; // the side of the soft edge being dragged, or under the cursor
    double m_dragWidth = 0, m_dragStart = 0;
    QUndoStack *m_undo = nullptr;
    bool m_cross = false;
    struct Drag { enum { None, Node, In, Out, Whole }; };
    bool m_maskMode = false, m_drawing = false;
    int m_maskDrag = Drag::None, m_maskSel = -1, m_nodeSel = -1;
    QPointF m_dragFrom, m_drawCursor;
    std::vector<MaskNode> m_drawNodes;
    std::vector<Mask> m_maskBefore;
    QToolButton *m_maskButton = nullptr;
    bool m_warpDrag = false;
    int m_warpDragIndex = -1, m_warpHover = -1;
    Mapping m_warpBefore;
    quint64 m_probed = 0; // the viewport that has the cross from this view
};

PreviewWorkspace::PreviewWorkspace(Engine *engine, MappingView *mapping, QWidget *parent)
    : QWidget(parent), m_engine(engine), m_mapping(mapping)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *count = m_countBox = new QComboBox;
    count->setObjectName(QStringLiteral("previewLayout"));
    count->addItems({QStringLiteral("1 view"), QStringLiteral("2 views"), QStringLiteral("3 views"), QStringLiteral("4 views")});
    auto *controls = new QHBoxLayout;
    controls->addWidget(count);
    m_fpsLabel = new QLabel;
    m_fpsLabel->setObjectName(QStringLiteral("previewFps"));
    m_fpsLabel->setToolTip(QStringLiteral("Rendering frame rate"));
    m_fpsLabel->setMinimumWidth(m_fpsLabel->fontMetrics().horizontalAdvance(QStringLiteral("000.0 fps")));
    controls->addWidget(m_fpsLabel);
    // A cross over a viewport's output, where the pointer is in its view: where to click to put a warp point
    auto *cross = new QToolButton;
    cross->setObjectName(QStringLiteral("previewCross"));
    cross->setText(QStringLiteral("✛ Cross"));
    cross->setCheckable(true);
    cross->setToolTip(QStringLiteral("Draw a cross on the output of a viewport where the pointer is in its view, "
                                     "to see where a warp point would go. It shows on the real output: switch it off for the show."));
    cross->setStyleSheet(QStringLiteral("QToolButton:checked { background:%1; color:%2; }").arg(theme::css(), theme::onAccent().name()));
    connect(cross, &QToolButton::toggled, this, [this](bool on) {
        for (Cell &c : m_cells) c.picture->setCross(on);
    });
    controls->addWidget(cross);
    // The blend guide on every viewport's output: a pattern that shows each overlap, to line the projectors up
    auto *guide = new QToolButton;
    guide->setObjectName(QStringLiteral("previewGuide"));
    guide->setText(QStringLiteral("▦ Guide"));
    guide->setCheckable(true);
    guide->setToolTip(QStringLiteral("Blend guide on every viewport's output: the overlap of each soft edge in the viewport's color, a grid, the ends "
                                     "of the ramps. For one viewport, or a white / grey / black field: Soft Edge in its Spatial tab. "
                                     "It shows on the real outputs: switch it off for the show."));
    guide->setStyleSheet(cross->styleSheet());
    connect(guide, &QToolButton::toggled, this, [this](bool on) {
        m_engine->setBlendGuide(0, on ? 1 : 0, false, true);
        emit softEdgeEdited(); // the inspectors show it
    });
    controls->addWidget(guide);
    controls->addStretch();
    layout->addLayout(controls);
    m_fpsTimer = new QTimer(this);
    m_fpsTimer->setInterval(250);
    connect(m_fpsTimer, &QTimer::timeout, this, [this] {
        m_fpsLabel->setText(QStringLiteral("%1 fps").arg(m_engine->fps(), 0, 'f', 1));
    });
    setShowFps(false);
    m_grid = new QGridLayout;
    m_grid->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(m_grid, 1);
    for (int slot = 0; slot < 4; ++slot) {
        auto &cell = m_cells[size_t(slot)];
        cell.widget = new QWidget(this);
        auto *column = new QVBoxLayout(cell.widget);
        column->setContentsMargins(0, 0, 0, 0);
        cell.source = new QToolButton;
        cell.source->setObjectName(QStringLiteral("previewSource%1").arg(slot));
        cell.source->setText(QStringLiteral("Composition"));
        cell.source->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(cell.source);
        cell.source->setMenu(menu);
        // The picture to look at: the composition, then the viewports, then the layers as the bin has them (groups, 3D
        // scenes with their objects)
        connect(menu, &QMenu::aboutToShow, this, [this, slot, menu] {
            showmenu::clearMenu(menu);
            auto *composition = menu->addAction(QStringLiteral("Composition"));
            connect(composition, &QAction::triggered, this, [this, slot] { selectSource(slot, 0, QStringLiteral("Composition")); });
            const auto labelOf = [this](quint64 id, const QString &name) {
                Engine::Lock lock(&m_engine->mutex());
                const Layer *l = m_engine->layer(m_engine->indexOfId(id));
                return l && (l->scene3d || l->merge3d) ? name + QStringLiteral(" (3D view)") : name;
            };
            const auto leaf = [this, slot, labelOf](QMenu *into, quint64 id, const QString &name) {
                const QString label = labelOf(id, name);
                connect(into->addAction(label), &QAction::triggered, this, [this, slot, id, label] { selectSource(slot, id, label); });
            };
            // A 3D scene or a Merge 3D can be looked at, above what it holds; a group is only a folder
            const auto container = [this, slot, labelOf](QMenu *into, quint64 id, const QString &name, bool isGroup) {
                if (isGroup) return;
                const QString label = labelOf(id, name);
                connect(into->addAction(label), &QAction::triggered, this, [this, slot, id, label] { selectSource(slot, id, label); });
                into->addSeparator();
            };
            auto *viewports = new QMenu(QStringLiteral("Viewports"), menu);
            showmenu::fillLayerTree(viewports, m_engine, leaf, container, true);
            if (viewports->isEmpty()) delete viewports;
            else menu->addMenu(viewports);
            auto *layers = new QMenu(QStringLiteral("Layers"), menu);
            showmenu::fillLayerTree(layers, m_engine, leaf, container, false, true); // 3D scenes and Merge 3D, not their objects
            if (layers->isEmpty()) delete layers;
            else menu->addMenu(layers);
        });
        column->addWidget(cell.source);
        cell.pages = new QStackedWidget;
        cell.picture = new PreviewPicture(engine, slot);
        cell.picture->onEdited = [this] { emit softEdgeEdited(); };
        cell.picture->onMasksEdited = [this] { emit masksEdited(); };
        cell.picture->onMaskPicked = [this](int index) { emit maskPicked(index); };
        cell.pages->addWidget(cell.picture);
        cell.scene3d = new Scene3DView(engine, slot);
        cell.pages->addWidget(cell.scene3d);
        connect(cell.scene3d, &Scene3DView::layerPicked, this, &PreviewWorkspace::layerPicked);
        if (slot == 0) cell.pages->addWidget(mapping);
        column->addWidget(cell.pages, 1);
    }
    connect(count, &QComboBox::currentIndexChanged, this, [this](int index) { setCount(index + 1); });
    selectSource(0, 0, QStringLiteral("Composition"));
    setCount(1);
}

PreviewWorkspace::~PreviewWorkspace()
{
    for (int slot = 0; slot < 4; ++slot) m_engine->setPreviewSource(slot, 0);
}

void PreviewWorkspace::selectSource(int slot, quint64 id, const QString &label)
{
    auto &cell = m_cells[size_t(slot)];
    cell.id = id;
    cell.source->setText(label);
    bool scene = false;
    if (id) {
        Engine::Lock lock(&m_engine->mutex());
        const Layer *l = m_engine->layer(m_engine->indexOfId(id));
        scene = l && (l->scene3d || l->merge3d); // a 3D view: a scene, or a Merge 3D of one
    }
    cell.pages->setCurrentWidget(scene                 ? static_cast<QWidget *>(cell.scene3d)
                                 : slot == 0 && !id ? static_cast<QWidget *>(m_mapping)
                                                    : cell.picture);
    m_engine->setPreviewSource(slot, id);
    cell.picture->setSource(id);
    cell.scene3d->setScene(scene ? id : 0); // a 3D scene: seen from the view's own camera
}

void PreviewWorkspace::setCount(int count)
{
    m_count = std::clamp(count, 1, 4);
    for (int slot = 0; slot < 4; ++slot) {
        auto &cell = m_cells[size_t(slot)];
        m_grid->removeWidget(cell.widget);
        cell.widget->setVisible(slot < m_count);
        if (slot < m_count) m_grid->addWidget(cell.widget, slot / 2, slot % 2, 1, m_count == 3 && slot == 2 ? 2 : 1);
        m_engine->setPreviewSource(slot, slot < m_count ? cell.id : 0);
    }
}

void PreviewWorkspace::refreshFrames()
{
    for (int slot = 0; slot < m_count; ++slot) {
        const auto &cell = m_cells[size_t(slot)];
        if (cell.picture->isVisible()) cell.picture->update();
        if (cell.scene3d->isVisible() && cell.scene3d->scene()) cell.scene3d->update();
    }
}

void PreviewWorkspace::setShowFps(bool visible)
{
    m_fpsLabel->setVisible(visible);
    if (visible) {
        m_fpsLabel->setText(QStringLiteral("%1 fps").arg(m_engine->fps(), 0, 'f', 1));
        m_fpsTimer->start();
    } else {
        m_fpsTimer->stop();
    }
}

void PreviewWorkspace::setUndoStack(QUndoStack *undo)
{
    for (Cell &c : m_cells) {
        c.scene3d->setUndoStack(undo);
        c.picture->setUndoStack(undo);
    }
}

void PreviewWorkspace::selectMask(int index)
{
    for (Cell &c : m_cells) c.picture->selectMask(index);
}

void PreviewWorkspace::setCurrentLayer(quint64 id)
{
    for (Cell &c : m_cells) c.scene3d->setSelected(id);
}

QJsonObject PreviewWorkspace::state() const
{
    QJsonArray cells;
    for (const Cell &c : m_cells)
        cells.append(QJsonObject{{"source", QString::number(c.id)}, {"view", c.picture->viewState()}, {"view3d", c.scene3d->viewState()}});
    return {{"count", m_count}, {"cells", cells}, {"mapping", m_mapping->viewState()}};
}

void PreviewWorkspace::setState(const QJsonObject &o)
{
    const QJsonArray cells = o.value("cells").toArray();
    for (int slot = 0; slot < int(m_cells.size()) && slot < cells.size(); ++slot) {
        const QJsonObject c = cells[slot].toObject();
        const quint64 id = c.value("source").toString().toULongLong();
        QString label = QStringLiteral("Composition");
        if (id) {
            Engine::Lock lock(&m_engine->mutex());
            const Layer *layer = m_engine->layer(m_engine->indexOfId(id));
            if (!layer) continue; // gone: the view keeps what it shows
            label = layer->name;
        }
        selectSource(slot, id, label);
        m_cells[size_t(slot)].picture->setViewState(c.value("view").toObject());
        m_cells[size_t(slot)].scene3d->setViewState(c.value("view3d").toObject());
    }
    if (o.contains("mapping")) m_mapping->setViewState(o.value("mapping").toObject());
    if (o.contains("count")) m_countBox->setCurrentIndex(std::clamp(o.value("count").toInt(1), 1, 4) - 1);
}
