#include "MaskEditor.h"
#include "Commands.h"
#include "Engine.h"
#include "Widgets.h"
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QToolButton>
#include <QUndoStack>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace {
const QColor kMask(41, 199, 224), kMaskFaint(41, 199, 224, 90);
}

MaskEditor::MaskEditor(Engine *engine, MaskEditorHost *host, QObject *parent) : QObject(parent), m_engine(engine), m_host(host) {}

Layer *MaskEditor::layer() const { return m_engine->layer(m_engine->indexOfId(m_host->maskLayerId())); }

Mask *MaskEditor::edited() const
{
    Layer *l = layer();
    if (!l || m_index < 0 || m_index >= int(l->masks.size())) return nullptr;
    return &l->masks[size_t(m_index)];
}

QToolButton *MaskEditor::makeButton(ZoomBar *bar, const QString &objectName)
{
    // The same two tools in every view: layer mapping, or mask node editing (mask nodes never move by accident while
    // the layer tool is active)
    m_button = bar->addButton(QStringLiteral("Layer"),
                              QStringLiteral("Layer mode: select and move layers. Press to switch to Mask mode. Hold Shift while dragging a node "
                                             "to align it with another point.\n"
                                             "In Mask mode: double-click the contour to add a point; right-click a point for Bézier, corner or deletion; "
                                             "Ctrl/⌘-click adds points to the selection; the arrows move them (Shift: ten pixels); Delete removes one."));
    m_button->setObjectName(objectName);
    m_button->setCheckable(true);
    connect(m_button, &QToolButton::toggled, this, &MaskEditor::setActive);
    return m_button;
}

void MaskEditor::setActive(bool on)
{
    if (m_active == on) return;
    m_active = on;
    m_drag = Drag::None;
    m_selection.clear();
    m_node = -1;
    if (!on && m_drawing) cancelDrawing();
    if (m_button) {
        QSignalBlocker block(m_button);
        m_button->setChecked(on);
        m_button->setText(on ? QStringLiteral("Mask") : QStringLiteral("Layer"));
    }
    emit activeChanged(on);
    emit changed();
}

void MaskEditor::setIndex(int index)
{
    m_index = index;
    m_selection.clear();
    if (index >= 0) setActive(true);
    m_node = -1;
    m_drag = Drag::None;
    emit changed();
}

void MaskEditor::reset()
{
    m_index = -1;
    m_node = -1;
    m_selection.clear();
    m_drag = Drag::None;
    if (m_drawing) cancelDrawing();
}

void MaskEditor::startDrawing()
{
    setActive(true);
    {
        Engine::Lock lk(&m_engine->mutex());
        if (!layer() || !m_host->maskEditable() || m_engine->isLocked(m_engine->indexOfId(m_host->maskLayerId()))) return;
    }
    QWidget *w = m_host->maskWidget();
    m_drawing = true;
    m_pressing = false;
    m_drawNodes.clear();
    m_tracking = w->hasMouseTracking();
    w->setMouseTracking(true);
    w->setCursor(Qt::CrossCursor);
    w->setFocus();
    emit changed();
}

void MaskEditor::cancelDrawing()
{
    m_drawing = m_pressing = false;
    m_drawNodes.clear();
    QWidget *w = m_host->maskWidget();
    w->setMouseTracking(m_tracking);
    w->unsetCursor();
    emit changed();
}

void MaskEditor::commit(quint64 id, const std::vector<Mask> &before, const std::vector<Mask> &after, const QString &text)
{
    if (m_undo) m_undo->push(new cmd::SetMasks(m_engine, id, before, after, text));
    else m_engine->setLayerMasks(id, after);
    emit masksEdited();
    emit changed();
}

void MaskEditor::finishDrawing()
{
    const std::vector<MaskNode> nodes = m_drawNodes;
    cancelDrawing();
    if (nodes.size() < 3) return;
    quint64 id = 0;
    std::vector<Mask> before, after;
    {
        Engine::Lock lk(&m_engine->mutex());
        Layer *l = layer();
        if (!l) return;
        id = l->id;
        before = after = l->masks;
        Mask k;
        k.shape = Mask::Shape::Path;
        k.nodes = nodes;
        k.name = masks::uniqueName(after);
        after.push_back(k);
    }
    commit(id, before, after, QStringLiteral("Draw Mask"));
    m_index = int(after.size()) - 1;
    emit maskPicked(m_index);
    emit changed();
}

// Its outline on the widget: the curves flattened, the long segments cut (a warped picture bends them)
QPolygonF MaskEditor::outline(const Mask &k) const
{
    QPolygonF out;
    if (!k.valid()) return out;
    const bool comp = m_host->maskInComposition(k);
    const double scale = 1000;
    for (const QPolygonF &poly : k.path(QSizeF(scale, scale)).toSubpathPolygons())
        for (int i = 0; i + 1 < poly.size(); ++i) {
            const QPointF a = poly[i] / scale, b = poly[i + 1] / scale;
            const int steps = std::max(1, int(std::ceil(std::hypot(b.x() - a.x(), b.y() - a.y()) / 0.02)));
            for (int s = 0; s < steps; ++s) out << m_host->maskToWidget(comp, a + (b - a) * (double(s) / steps));
        }
    if (!out.isEmpty()) out << out.front();
    return out;
}

// ---- Events ----------------------------------------------------------------------------------------------------------

bool MaskEditor::mousePress(QMouseEvent *e)
{
    if (!m_active && !m_drawing) return false;
    Engine::Lock lk(&m_engine->mutex());
    const QPointF p = e->position();
    const int layerIndex = m_engine->indexOfId(m_host->maskLayerId());
    if (!m_drawing && e->button() == Qt::LeftButton && !m_engine->isLocked(layerIndex)) {
        // The mode button is enough to pick a mask: no selection in the inspector is needed first
        if (const Layer *l = layer()) {
            for (int i = int(l->masks.size()) - 1; i >= 0; --i) {
                const Mask &mask = l->masks[size_t(i)];
                if (!mask.enabled || !m_host->maskShown(mask)) continue;
                bool hit = outline(mask).containsPoint(p, Qt::OddEvenFill);
                for (const auto &node : mask.nodes)
                    hit |= QLineF(m_host->maskToWidget(m_host->maskInComposition(mask), node.p), p).length() <= 8;
                if (hit) {
                    if (i != m_index) {
                        setIndex(i);
                        lk.unlock();
                        emit maskPicked(i);
                        lk.relock();
                    }
                    break;
                }
            }
        }
    }
    press(e);
    emit changed();
    return true; // in Mask mode a press never begins a layer move
}

// A mask's point, handle or inside, or the point of one being drawn (lock held)
bool MaskEditor::press(QMouseEvent *e)
{
    const QPointF p = e->position();
    Layer *l = layer();
    const int layerIndex = m_engine->indexOfId(m_host->maskLayerId());
    if (!l || !m_host->maskEditable()) return false;
    if (m_engine->isLocked(layerIndex)) {
        if (m_drawing) cancelDrawing();
        return false;
    }
    auto near = [&](QPointF a, double r) { return std::hypot(a.x() - p.x(), a.y() - p.y()) <= r; };
    if (m_drawing) {
        if (e->button() != Qt::LeftButton) return true;
        if (m_drawNodes.size() >= 3 && near(m_host->maskToWidget(false, m_drawNodes.front().p), 8)) { // on the first point
            finishDrawing();
            return true;
        }
        const QPointF guess = m_drawNodes.empty() ? QPointF(0.5, 0.5) : m_drawNodes.back().p;
        QPointF q = m_host->maskFromWidget(false, p, guess);
        if (m_host->maskSnapping(e->modifiers())) {
            const QPointF caught = m_host->maskSnap(m_host->maskToWidget(false, q), {}, true);
            q = m_host->maskFromWidget(false, caught, guess);
        }
        m_drawNodes.push_back({q, q, q});
        m_pressing = true;
        m_cursor = p;
        return true;
    }
    Mask *k = edited();
    if (!k || !k->enabled || !m_host->maskShown(*k) || e->button() != Qt::LeftButton) return false;
    const bool comp = m_host->maskInComposition(*k);
    m_drag = Drag::None;
    for (int i = 0; i < int(k->nodes.size()) && m_drag == Drag::None; ++i) { // the handles first
        const MaskNode &n = k->nodes[size_t(i)];
        if (n.out != n.p && near(m_host->maskToWidget(comp, n.out), 6)) m_drag = Drag::Out;
        else if (n.in != n.p && near(m_host->maskToWidget(comp, n.in), 6)) m_drag = Drag::In;
        if (m_drag != Drag::None) m_node = i;
    }
    for (int i = 0; i < int(k->nodes.size()) && m_drag == Drag::None; ++i)
        if (near(m_host->maskToWidget(comp, k->nodes[size_t(i)].p), 7)) {
            m_drag = Drag::Node;
            m_node = i;
        }
    if (m_drag == Drag::None && outline(*k).containsPoint(p, Qt::OddEvenFill)) {
        m_drag = Drag::Body;
        m_last = m_host->maskFromWidget(comp, p, k->bounds().center());
    }
    if (m_drag == Drag::None) return false;
    if (m_drag == Drag::Node) {
        const bool extend = e->modifiers() & Qt::ControlModifier;
        if (!extend && std::find(m_selection.begin(), m_selection.end(), m_node) == m_selection.end()) {
            m_selection = {m_node};
        } else if (extend) {
            const auto it = std::find(m_selection.begin(), m_selection.end(), m_node);
            if (it == m_selection.end()) {
                m_selection.push_back(m_node);
            } else {
                m_selection.erase(it);
                m_drag = Drag::None;
            }
        }
    } else if (m_drag != Drag::Body) {
        m_selection = {m_node};
    }
    m_before = l->masks;
    return true;
}

bool MaskEditor::mouseMove(QMouseEvent *e)
{
    if (m_drawing) { // the line to the cursor; pressed: the last point's handles follow it
        m_cursor = e->position();
        if (m_pressing && !m_drawNodes.empty()) {
            Engine::Lock lk(&m_engine->mutex());
            MaskNode &n = m_drawNodes.back();
            const QPointF w = m_host->maskToWidget(false, n.p);
            if (std::hypot(m_cursor.x() - w.x(), m_cursor.y() - w.y()) > 3) {
                n.out = m_host->maskFromWidget(false, m_cursor, n.p);
                n.in = 2 * n.p - n.out;
            }
        }
        emit changed();
        return true;
    }
    if (m_drag == Drag::None) return false;
    Engine::Lock lk(&m_engine->mutex());
    Mask *k = edited();
    Layer *l = layer();
    if (!k || !l || m_node >= int(k->nodes.size())) return true;
    const bool comp = m_host->maskInComposition(*k);
    const QPointF w = e->position();
    if (m_drag == Drag::Body) {
        const QPointF q = m_host->maskFromWidget(comp, w, m_last);
        k->translate(q - m_last);
        m_last = q;
    } else {
        MaskNode &n = k->nodes[size_t(m_node)];
        // Magnetism works in the view's space even when a surface mask is warped by mapping: Shift aligns against stationary
        // anchors (the other points of the masks), in screen axes
        auto snapped = [&](QPointF raw) {
            if (!m_host->maskSnapping(e->modifiers())) {
                m_host->maskClearGuides();
                return raw;
            }
            std::vector<QPointF> others;
            for (const Mask &other : l->masks)
                for (size_t j = 0; j < other.nodes.size(); ++j) {
                    if (&other == k && m_drag == Drag::Node &&
                        (int(j) == m_node || std::find(m_selection.begin(), m_selection.end(), int(j)) != m_selection.end()))
                        continue;
                    others.push_back(m_host->maskToWidget(m_host->maskInComposition(other), other.nodes[j].p));
                }
            return m_host->maskFromWidget(comp, m_host->maskSnap(m_host->maskToWidget(comp, raw), others, false), raw);
        };
        if (m_drag == Drag::Node) {
            const QPointF d = snapped(m_host->maskFromWidget(comp, w, n.p)) - n.p;
            if (m_selection.empty()) m_selection = {m_node};
            for (int j : m_selection) {
                if (j < 0 || j >= int(k->nodes.size())) continue;
                MaskNode &node = k->nodes[size_t(j)];
                node.p += d;
                node.in += d;
                node.out += d;
            }
        } else { // a handle: the other one mirrors it (Shift: on its own)
            QPointF &h = m_drag == Drag::Out ? n.out : n.in;
            QPointF &o = m_drag == Drag::Out ? n.in : n.out;
            h = snapped(m_host->maskFromWidget(comp, w, h));
            if (!(e->modifiers() & Qt::ShiftModifier) && o != n.p) o = 2 * n.p - h;
        }
    }
    ++l->masksRevision;
    lk.unlock();
    emit masksEdited();
    emit changed();
    return true;
}

bool MaskEditor::mouseRelease(QMouseEvent *)
{
    if (m_drawing) {
        m_pressing = false;
        return true;
    }
    if (m_drag == Drag::None) return false;
    const bool whole = m_drag == Drag::Body;
    m_drag = Drag::None;
    quint64 id = 0;
    std::vector<Mask> after;
    {
        Engine::Lock lk(&m_engine->mutex());
        if (Layer *l = layer()) {
            id = l->id;
            after = l->masks;
        }
    }
    if (id && m_undo && after != m_before)
        m_undo->push(new cmd::SetMasks(m_engine, id, m_before, after, whole ? QStringLiteral("Move Mask") : QStringLiteral("Move Mask Point")));
    m_host->maskClearGuides();
    emit masksEdited();
    emit changed();
    return true;
}

bool MaskEditor::mouseDoubleClick(QMouseEvent *e)
{
    if (m_drawing) {
        finishDrawing();
        return true;
    }
    if (!m_active || e->button() != Qt::LeftButton) return false;
    Engine::Lock lock(&m_engine->mutex());
    Layer *l = layer();
    Mask *mask = edited();
    if (!l || !mask || !mask->enabled || !m_host->maskShown(*mask) || m_engine->isLocked(m_engine->indexOfId(m_host->maskLayerId()))) return true;
    const bool comp = m_host->maskInComposition(*mask);
    double best = 9.0, tBest = 0;
    int segment = -1;
    for (int i = 0; i < int(mask->nodes.size()); ++i) { // the nearest place of the contour
        const auto &a = mask->nodes[size_t(i)];
        const auto &b = mask->nodes[(size_t(i) + 1) % mask->nodes.size()];
        for (int step = 1; step < 128; ++step) {
            const double t = step / 128.0, u = 1 - t;
            const QPointF q = a.p * u * u * u + a.out * 3 * u * u * t + b.in * 3 * u * t * t + b.p * t * t * t;
            const double distance = QLineF(m_host->maskToWidget(comp, q), e->position()).length();
            if (distance < best) {
                best = distance;
                segment = i;
                tBest = t;
            }
        }
    }
    if (segment >= 0) {
        const auto before = l->masks;
        auto after = before;
        masks::splitSegment(after[size_t(m_index)], segment, tBest);
        const auto id = l->id;
        lock.unlock();
        commit(id, before, after, QStringLiteral("Add Mask Point"));
        m_node = segment + 1;
        m_selection = {m_node};
        m_drag = Drag::None;
    }
    return true;
}

bool MaskEditor::keyPress(QKeyEvent *e)
{
    if (m_drawing) {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) finishDrawing();
        else if (e->key() == Qt::Key_Escape) cancelDrawing();
        return true;
    }
    if (!m_active) return false;
    const int key = e->key();
    // The arrow keys nudge the points picked, not the layer geometry
    if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) {
        quint64 id = 0;
        std::vector<Mask> before, after;
        {
            Engine::Lock lk(&m_engine->mutex());
            Layer *l = layer();
            Mask *k = edited();
            if (!l || !k || m_engine->isLocked(m_engine->indexOfId(m_host->maskLayerId())) || m_selection.empty()) return true;
            id = l->id;
            before = after = l->masks;
            const QSize frame = m_host->maskFrameSize(*k);
            if (frame.width() <= 0 || frame.height() <= 0) return true;
            const double amount = e->modifiers() & Qt::ShiftModifier ? 10.0 : 1.0;
            const QPointF delta((key == Qt::Key_Right ? amount : key == Qt::Key_Left ? -amount : 0) / frame.width(),
                                (key == Qt::Key_Down ? amount : key == Qt::Key_Up ? -amount : 0) / frame.height());
            Mask &target = after[size_t(m_index)];
            for (int i : m_selection) {
                if (i < 0 || i >= int(target.nodes.size())) continue;
                MaskNode &n = target.nodes[size_t(i)];
                n.p += delta;
                n.in += delta;
                n.out += delta;
            }
            target.shape = Mask::Shape::Path;
        }
        if (before != after) commit(id, before, after, QStringLiteral("Nudge Mask Nodes"));
        return true;
    }
    if (m_index >= 0 && m_node >= 0 && (key == Qt::Key_Delete || key == Qt::Key_Backspace)) {
        quint64 id = 0;
        std::vector<Mask> before, after;
        {
            Engine::Lock lk(&m_engine->mutex());
            Layer *l = layer();
            Mask *k = edited();
            if (l && k && !m_engine->isLocked(m_engine->indexOfId(m_host->maskLayerId())) && k->nodes.size() > 3 &&
                m_node < int(k->nodes.size())) {
                id = l->id;
                before = after = l->masks;
                Mask &a = after[size_t(m_index)];
                a.nodes.erase(a.nodes.begin() + m_node);
                a.shape = Mask::Shape::Path;
            }
        }
        if (id) {
            m_node = -1;
            m_selection.clear();
            commit(id, before, after, QStringLiteral("Delete Mask Point"));
            return true;
        }
    }
    return false;
}

bool MaskEditor::contextMenu(QContextMenuEvent *event)
{
    if (!m_active || m_drawing) return false;
    Engine::Lock lock(&m_engine->mutex());
    Layer *l = layer();
    Mask *mask = edited();
    if (!l || !mask || !mask->enabled || !m_host->maskShown(*mask) || m_engine->isLocked(m_engine->indexOfId(m_host->maskLayerId()))) return true;
    int node = -1;
    for (int i = 0; i < int(mask->nodes.size()); ++i)
        if (QLineF(m_host->maskToWidget(m_host->maskInComposition(*mask), mask->nodes[size_t(i)].p), event->pos()).length() <= 8) {
            node = i;
            break;
        }
    if (node < 0) return true;
    const auto before = l->masks;
    const auto id = l->id;
    const int maskIndex = m_index;
    lock.unlock(); // the menu runs a nested event loop
    QMenu menu(m_host->maskWidget());
    auto *smooth = menu.addAction(QStringLiteral("Smooth Point (Bézier)"));
    auto *corner = menu.addAction(QStringLiteral("Corner Point"));
    auto *remove = menu.addAction(QStringLiteral("Delete Point"));
    remove->setEnabled(before[size_t(maskIndex)].nodes.size() > 3);
    const auto *chosen = menu.exec(event->globalPos());
    if (!chosen) return true;
    // A recall may have replaced the geometry while the menu was open
    lock.relock();
    l = layer();
    if (!l || m_engine->isLocked(m_engine->indexOfId(id)) || l->masks != before) return true;
    auto after = before;
    auto &editedMask = after[size_t(maskIndex)];
    auto &point = editedMask.nodes[size_t(node)];
    if (chosen == smooth) {
        const auto prev = editedMask.nodes[(size_t(node) + editedMask.nodes.size() - 1) % editedMask.nodes.size()].p;
        const auto next = editedMask.nodes[(size_t(node) + 1) % editedMask.nodes.size()].p;
        const QPointF tangent = (next - prev) / 6;
        point.in = point.p - tangent;
        point.out = point.p + tangent;
    } else if (chosen == corner) {
        point.in = point.out = point.p;
    } else if (chosen == remove) {
        editedMask.nodes.erase(editedMask.nodes.begin() + node);
    }
    editedMask.shape = Mask::Shape::Path;
    lock.unlock();
    commit(id, before, after, chosen->text());
    m_node = -1;
    m_selection.clear();
    return true;
}

bool MaskEditor::nodePixels(QPointF *pixels, QString *name) const
{
    if (!m_active) return false;
    Engine::Lock lk(&m_engine->mutex());
    const Mask *k = edited();
    if (!k || m_node < 0 || m_node >= int(k->nodes.size()) || m_engine->isLocked(m_engine->indexOfId(m_host->maskLayerId()))) return false;
    const QSize frame = m_host->maskFrameSize(*k);
    const QPointF p = k->nodes[size_t(m_node)].p;
    *pixels = QPointF(p.x() * frame.width(), p.y() * frame.height());
    *name = QStringLiteral("Mask node %1").arg(m_node + 1);
    return true;
}

bool MaskEditor::moveNodeToPixels(QPointF pixels)
{
    quint64 id = 0;
    std::vector<Mask> before, after;
    {
        Engine::Lock lk(&m_engine->mutex());
        Mask *k = edited();
        Layer *l = layer();
        if (!k || !l || m_node < 0 || m_node >= int(k->nodes.size()) || m_engine->isLocked(m_engine->indexOfId(m_host->maskLayerId()))) return false;
        id = l->id;
        before = after = l->masks;
        const QSize frame = m_host->maskFrameSize(*k);
        if (frame.width() <= 0 || frame.height() <= 0) return false;
        MaskNode &n = after[size_t(m_index)].nodes[size_t(m_node)];
        const QPointF delta(pixels.x() / frame.width() - n.p.x(), pixels.y() / frame.height() - n.p.y());
        n.p += delta;
        n.in += delta;
        n.out += delta;
    }
    if (before == after) return false;
    commit(id, before, after, QStringLiteral("Move Mask Node"));
    return true;
}

// ---- Painting --------------------------------------------------------------------------------------------------------

std::vector<MaskPaint> MaskEditor::paint() const
{
    std::vector<MaskPaint> out;
    Engine::Lock lk(&m_engine->mutex());
    const Layer *l = layer();
    if (!l || !m_host->maskEditable()) return out;
    // Its masks: the edited one bright, the others faint
    MaskPaint faint{MaskPaint::Kind::Lines, kMaskFaint, 0, {}}, bright{MaskPaint::Kind::Lines, kMask, 0, {}};
    for (int i = 0; i < int(l->masks.size()); ++i) {
        const Mask &k = l->masks[size_t(i)];
        if (!m_host->maskShown(k)) continue;
        const QPolygonF poly = outline(k);
        auto &lines = i == m_index ? bright.points : faint.points;
        for (int j = 0; j + 1 < poly.size(); ++j) {
            lines.push_back(poly[j]);
            lines.push_back(poly[j + 1]);
        }
    }
    out.push_back(faint);
    out.push_back(bright);
    if (m_active && m_index >= 0 && m_index < int(l->masks.size()) && m_host->maskShown(l->masks[size_t(m_index)])) {
        const Mask &k = l->masks[size_t(m_index)];
        const bool comp = m_host->maskInComposition(k);
        MaskPaint stems{MaskPaint::Kind::Lines, kMaskFaint, 0, {}}, handles{MaskPaint::Kind::Squares, kMask, 3.0f, {}},
            shadow{MaskPaint::Kind::Squares, QColor(0, 0, 0, 170), 5.5f, {}}, points{MaskPaint::Kind::Squares, QColor(255, 255, 255, 235), 4.0f, {}},
            picked{MaskPaint::Kind::Squares, kMask, 4.0f, {}};
        for (int i = 0; i < int(k.nodes.size()); ++i) {
            const MaskNode &n = k.nodes[size_t(i)];
            const QPointF p = m_host->maskToWidget(comp, n.p);
            for (const QPointF &hq : {n.in, n.out})
                if (hq != n.p) {
                    const QPointF h = m_host->maskToWidget(comp, hq);
                    stems.points.push_back(p);
                    stems.points.push_back(h);
                    handles.points.push_back(h);
                }
            shadow.points.push_back(p);
            (std::find(m_selection.begin(), m_selection.end(), i) != m_selection.end() ? picked : points).points.push_back(p);
        }
        for (const MaskPaint &part : {stems, handles, shadow, points, picked}) out.push_back(part);
    }
    // Being drawn: its points so far, and the line to the cursor
    if (m_drawing && !m_drawNodes.empty()) {
        MaskPaint lines{MaskPaint::Kind::Lines, kMask, 0, {}}, points{MaskPaint::Kind::Squares, QColor(255, 255, 255, 235), 4.0f, {}},
            first{MaskPaint::Kind::Squares, QColor(255, 255, 255, 235), 6.0f, {}};
        QPointF prev;
        for (size_t i = 0; i < m_drawNodes.size(); ++i) {
            const QPointF p = m_host->maskToWidget(false, m_drawNodes[i].p);
            if (i) {
                lines.points.push_back(prev);
                lines.points.push_back(p);
            }
            (i == 0 && m_drawNodes.size() >= 3 ? first : points).points.push_back(p); // the first one closes it
            if (m_drawNodes[i].out != m_drawNodes[i].p) {
                lines.points.push_back(m_host->maskToWidget(false, m_drawNodes[i].in));
                lines.points.push_back(m_host->maskToWidget(false, m_drawNodes[i].out));
            }
            prev = p;
        }
        if (!m_pressing) {
            lines.points.push_back(prev);
            lines.points.push_back(m_cursor);
        }
        for (const MaskPaint &part : {lines, points, first}) out.push_back(part);
    }
    return out;
}
