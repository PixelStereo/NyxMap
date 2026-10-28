// The Mapping view's lines: a Line generator's (LineGenerator.h) drawn, edited and shown over its layer (MappingView.h)
#include "MappingView.h"

#include "Commands.h"
#include "Engine.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QUndoStack>
#include <cmath>

static const QColor kLine(120, 230, 120), kLineFaint(120, 230, 120, 120), kLinePicked(255, 150, 40);
static constexpr double kPointHit = 8, kSegmentHit = 6; // widget pixels

static double distanceToSegment(QPointF p, QPointF a, QPointF b, double *t = nullptr)
{
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    const double k = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    if (t) *t = k;
    const QPointF q = a + ab * k;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

bool MappingView::isLineLayer() const
{
    const Layer *l = m_engine->layer(m_layer);
    return l && l->type == SourceType::Isf && isLineGeneratorPath(l->sourcePath) && mapping();
}

MappingView::LinePick MappingView::hitLine(QPointF w) const
{
    const Layer *l = m_engine->layer(m_layer);
    if (!l) return {};
    for (int i = int(l->lines.size()) - 1; i >= 0; --i) // the top one: drawn last
        for (int k = 0; k < int(l->lines[size_t(i)].points.size()); ++k) {
            const QPointF p = frameToWidget(false, l->lines[size_t(i)].points[size_t(k)]);
            if (std::hypot(p.x() - w.x(), p.y() - w.y()) <= kPointHit) return {i, k};
        }
    for (int i = int(l->lines.size()) - 1; i >= 0; --i) {
        const LinePath &line = l->lines[size_t(i)];
        for (int k = 0; k < line.segmentCount(); ++k)
            if (distanceToSegment(w, frameToWidget(false, line.segmentStart(k)), frameToWidget(false, line.segmentEnd(k))) <= kSegmentHit)
                return {i, -1};
    }
    return {};
}

void MappingView::startLineDrawing()
{
    {
        Engine::Lock lk(&m_engine->mutex());
        if (!isLineLayer() || m_engine->isLocked(m_layer)) return;
    }
    if (m_drawing) cancelDrawing();
    m_lineDrawing = true;
    m_linePoints.clear();
    m_lineSel = {};
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setFocus();
    update();
}

void MappingView::cancelLineDrawing()
{
    m_lineDrawing = false;
    m_linePoints.clear();
    setMouseTracking(false);
    unsetCursor();
    update();
}

void MappingView::finishLineDrawing()
{
    LinePath line;
    line.points = m_linePoints;
    cancelLineDrawing();
    if (line.points.size() < 2) return;
    // Closed: ended on its first point (taken off again)
    if (line.points.size() >= 4 && line.points.back() == line.points.front()) {
        line.points.pop_back();
        line.closed = true;
    }
    std::vector<LinePath> before;
    {
        Engine::Lock lk(&m_engine->mutex());
        if (const Layer *l = m_engine->layer(m_layer)) before = l->lines;
    }
    std::vector<LinePath> after = before;
    after.push_back(line);
    commitLines(before, after, QStringLiteral("Draw Line"));
    m_lineSel = {int(after.size()) - 1, -1};
}

void MappingView::commitLines(const std::vector<LinePath> &before, const std::vector<LinePath> &after, const QString &text)
{
    const quint64 id = m_engine->layerId(m_layer);
    if (!id) return;
    if (m_undo && before != after) m_undo->push(new cmd::SetLines(m_engine, id, before, after, text));
    else m_engine->setLayerLines(id, after);
    emit linesEdited();
    update();
}

bool MappingView::pressLine(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return false;
    Engine::Lock lk(&m_engine->mutex());
    if (!isLineLayer() || m_maskEditMode || m_drawing) return false;
    const QPointF w = e->position();
    if (m_lineDrawing) {
        const QPointF guess = m_linePoints.empty() ? QPointF(0.5, 0.5) : m_linePoints.back();
        if (m_linePoints.size() >= 3) { // the first point again: closed
            const QPointF first = frameToWidget(false, m_linePoints.front());
            if (std::hypot(first.x() - w.x(), first.y() - w.y()) <= kPointHit) {
                m_linePoints.push_back(m_linePoints.front());
                lk.unlock();
                finishLineDrawing();
                return true;
            }
        }
        m_linePoints.push_back(widgetToFrame(false, w, guess));
        update();
        return true;
    }
    if (m_engine->isLocked(m_layer)) return false;
    const LinePick pick = hitLine(w);
    m_lineSel = pick;
    if (!pick.valid()) return false;
    const Layer *l = m_engine->layer(m_layer);
    m_linesBefore = l->lines;
    const LinePath &line = l->lines[size_t(pick.line)];
    m_lineGrab = widgetToFrame(false, w, pick.point >= 0 ? line.points[size_t(pick.point)] : line.points.front());
    m_lineDragging = true;
    update();
    return true;
}

bool MappingView::dragLine(QMouseEvent *e)
{
    if (m_lineDrawing) {
        m_cursor = e->position();
        update();
        return true;
    }
    if (!m_lineDragging) return false;
    {
        Engine::Lock lk(&m_engine->mutex());
        Layer *l = m_engine->layer(m_layer);
        if (!l || m_lineSel.line >= int(l->lines.size())) return true;
        LinePath &line = l->lines[size_t(m_lineSel.line)];
        const QPointF q = widgetToFrame(false, e->position(), m_lineGrab);
        const QPointF delta = q - m_lineGrab;
        if (m_lineSel.point >= 0 && m_lineSel.point < int(line.points.size())) line.points[size_t(m_lineSel.point)] += delta;
        else
            for (QPointF &p : line.points) p += delta;
        m_lineGrab = q;
        ++l->linesRevision; // drawn as it moves
    }
    update();
    return true;
}

bool MappingView::releaseLine()
{
    if (m_lineDrawing) return true;
    if (!m_lineDragging) return false;
    m_lineDragging = false;
    std::vector<LinePath> after;
    {
        Engine::Lock lk(&m_engine->mutex());
        if (const Layer *l = m_engine->layer(m_layer)) after = l->lines;
    }
    if (after != m_linesBefore)
        commitLines(m_linesBefore, after, m_lineSel.point >= 0 ? QStringLiteral("Move Line Point") : QStringLiteral("Move Line"));
    return true;
}

bool MappingView::doubleClickLine(QMouseEvent *e)
{
    if (m_lineDrawing) {
        // The double-click's first press added a point already: the second one is not another
        if (m_linePoints.size() >= 2) {
            const QPointF a = m_linePoints[m_linePoints.size() - 1], b = m_linePoints[m_linePoints.size() - 2];
            Engine::Lock lk(&m_engine->mutex());
            const QPointF wa = frameToWidget(false, a), wb = frameToWidget(false, b);
            if (std::hypot(wa.x() - wb.x(), wa.y() - wb.y()) < 4) m_linePoints.pop_back();
        }
        finishLineDrawing();
        return true;
    }
    if (e->button() != Qt::LeftButton) return false;
    std::vector<LinePath> before, after;
    {
        Engine::Lock lk(&m_engine->mutex());
        if (!isLineLayer() || m_engine->isLocked(m_layer) || m_maskEditMode) return false;
        const Layer *l = m_engine->layer(m_layer);
        const QPointF w = e->position();
        // A segment double-clicked: a point there
        for (int i = int(l->lines.size()) - 1; i >= 0 && after.empty(); --i) {
            const LinePath &line = l->lines[size_t(i)];
            for (int k = 0; k < line.segmentCount(); ++k) {
                double t = 0;
                const QPointF a = line.segmentStart(k), b = line.segmentEnd(k);
                if (distanceToSegment(w, frameToWidget(false, a), frameToWidget(false, b), &t) > kSegmentHit) continue;
                before = after = l->lines;
                std::vector<QPointF> &pts = after[size_t(i)].points;
                pts.insert(pts.begin() + k + 1, a + (b - a) * t);
                m_lineSel = {i, k + 1};
                break;
            }
        }
    }
    if (after.empty()) return false;
    commitLines(before, after, QStringLiteral("Add Line Point"));
    return true;
}

bool MappingView::keyLine(QKeyEvent *e)
{
    const int key = e->key();
    if (m_lineDrawing) {
        if (key == Qt::Key_Return || key == Qt::Key_Enter) finishLineDrawing();
        else if (key == Qt::Key_Escape) cancelLineDrawing();
        else if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
            if (!m_linePoints.empty()) m_linePoints.pop_back();
            update();
        }
        return true;
    }
    if (!m_lineSel.valid()) return false;
    if (key == Qt::Key_Escape) {
        m_lineSel = {};
        update();
        return true;
    }
    if (key != Qt::Key_Delete && key != Qt::Key_Backspace) return false;
    std::vector<LinePath> before, after;
    {
        Engine::Lock lk(&m_engine->mutex());
        const Layer *l = m_engine->layer(m_layer);
        if (!isLineLayer() || m_engine->isLocked(m_layer) || m_lineSel.line >= int(l->lines.size())) return false;
        before = after = l->lines;
    }
    LinePath &line = after[size_t(m_lineSel.line)];
    const bool point = m_lineSel.point >= 0 && m_lineSel.point < int(line.points.size());
    if (point) line.points.erase(line.points.begin() + m_lineSel.point);
    if (!point || line.points.size() < 2) after.erase(after.begin() + m_lineSel.line); // too short to be one
    else if (line.points.size() < 3) line.closed = false;
    m_lineSel = {};
    commitLines(before, after, point ? QStringLiteral("Delete Line Point") : QStringLiteral("Delete Line"));
    return true;
}

void MappingView::paintLines()
{
    Engine::Lock lk(&m_engine->mutex());
    if (!isLineLayer()) return;
    const Layer *l = m_engine->layer(m_layer);
    // Each segment cut in steps: a warped picture bends it
    auto segment = [&](std::vector<float> &out, QPointF a, QPointF b) {
        const int steps = 16;
        QPointF prev = frameToWidget(false, a);
        for (int s = 1; s <= steps; ++s) {
            const QPointF p = frameToWidget(false, a + (b - a) * (double(s) / steps));
            pushLine(out, prev, p);
            prev = p;
        }
    };
    std::vector<float> faint, picked, points, shadow, selected;
    for (int i = 0; i < int(l->lines.size()); ++i) {
        const LinePath &line = l->lines[size_t(i)];
        for (int k = 0; k < line.segmentCount(); ++k)
            segment(i == m_lineSel.line ? picked : faint, line.segmentStart(k), line.segmentEnd(k));
        for (int k = 0; k < int(line.points.size()); ++k) {
            const QPointF p = frameToWidget(false, line.points[size_t(k)]);
            pushRect(shadow, p, 5.0f);
            pushRect(i == m_lineSel.line && k == m_lineSel.point ? selected : points, p, k == 0 ? 4.5f : 3.5f);
        }
    }
    m_draw.drawLines(faint, kLineFaint);
    m_draw.drawLines(picked, kLine);
    // Being drawn: its points so far, and the segment to the cursor
    std::vector<float> drawing;
    for (size_t k = 0; k < m_linePoints.size(); ++k) {
        if (k) segment(drawing, m_linePoints[k - 1], m_linePoints[k]);
        const QPointF p = frameToWidget(false, m_linePoints[k]);
        pushRect(shadow, p, 5.5f);
        pushRect(points, p, k == 0 && m_linePoints.size() >= 3 ? 6.0f : 4.0f); // the first one closes it
    }
    if (m_lineDrawing && !m_linePoints.empty()) pushLine(drawing, frameToWidget(false, m_linePoints.back()), m_cursor);
    m_draw.drawLines(drawing, kLine);
    m_draw.drawTriangles(shadow, QColor(0, 0, 0, 170));
    m_draw.drawTriangles(points, QColor(255, 255, 255, 235));
    m_draw.drawTriangles(selected, kLinePicked);
}
