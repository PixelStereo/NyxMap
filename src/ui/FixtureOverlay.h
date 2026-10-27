#pragma once
// Pixel mapping fixtures (Fixtures.h) drawn over a picture of the composition — the mapping view, a preview of the
// composition or of a viewport — and picked there. Drawn by the interface only: never in the composition, the outputs
// or the DMX.

#include "Fixtures.h"

#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <functional>

namespace fixtureview {
using ToWidget = std::function<QPointF(QPointF)>; // a composition point (normalized, origin top left) in the widget

QPolygonF outline(const Fixture &f, QSize comp, const ToWidget &toWidget);
// The fixture under a widget point, the topmost one (drawn last); 0: none. Thin fixtures are caught a few pixels away
quint64 hit(const FixturePatch &p, QSize comp, const ToWidget &toWidget, QPointF at);
QString label(const Fixture &f); // "Name · 1.001"
// selected: drawn highlighted, with the grips of the mapping view (rotate, resize) when grips is true
void paint(QPainter &painter, const FixturePatch &p, QSize comp, const ToWidget &toWidget, quint64 selected = 0,
           bool grips = false);
// The grips of a fixture in the widget: resize at its bottom right corner, rotate above the middle of its top edge
QPointF resizeGrip(const Fixture &f, QSize comp, const ToWidget &toWidget);
QPointF rotateGrip(const Fixture &f, QSize comp, const ToWidget &toWidget);
} // namespace fixtureview
