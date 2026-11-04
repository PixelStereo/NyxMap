#pragma once
// An envelope to take in the hand: a curve whose points are dragged. An instrument's, one of Faust's envelopes, drawn with their
// own equations (envelopes.lib), or an interaction's Envelope filter.
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

class AdsrCurve : public QWidget
{
public:
    // The shape of its segments. Straight: en.adsr. Exponential: en.adsre, en.ahdsre (an envelope of T60s), and the fall of the
    // Envelope filter. Biased: en.*_bias
    enum class Shape { Straight, Exponential, Biased };
    struct Values {
        double attack = 0.01, decay = 0.1, sustain = 0.8, release = 0.1; // seconds, and 0..1 for the sustain
        double hold = 0, finalLevel = 0;                                 // an AHDSR's, an *f_bias's
        double biasAttack = 0.5, biasDecay = 0.5, biasRelease = 0.5;     // 0.5: straight
    };
    std::function<void(const Values &)> edited; // as it is dragged
    double longest = 10;                        // the longest attack, decay or release
    Shape shape = Shape::Exponential;
    bool hasHold = false, hasFinal = false;

    explicit AdsrCurve(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("adsrCurve"));
        setMinimumHeight(120);
        setMouseTracking(true);
        setToolTip(QStringLiteral("Drag the points: the top of the attack, the end of the hold and of the decay (and the level held), the end of the release; "
                                  "the points on the slopes bend them"));
    }
    void setValues(const Values &v)
    {
        if (m_drag >= 0) return; // the hand has it
        m_v = v;
        update();
    }
    Values values() const { return m_v; }

    // Its level along a segment. u: 0 to 1 along the segment's time; from, to: its levels
    double along(int segment, double u, double from, double to) const
    {
        u = std::clamp(u, 0.0, 1.0);
        if (shape == Shape::Exponential) return to + (from - to) * std::exp(-6.91 * u);
        if (shape == Shape::Straight) return from + (to - from) * u;
        // bias_curve(b, x): x as a part of the way from the lower level to the higher. Above 0.5 it goes up sooner
        const double b = std::clamp(segment == 0 ? m_v.biasAttack : segment == 1 ? m_v.biasDecay : m_v.biasRelease, 0.03, 0.97);
        const double x = to >= from ? u : 1 - u;
        const double y = x / ((1.0 / b - 2.0) * (1.0 - x) + 1.0);
        return to >= from ? from + (to - from) * y : to + (from - to) * y;
    }
    // Where the points are, in its own pixels
    enum Point { AttackTop, HoldEnd, DecayEnd, ReleaseEnd, BendAttack, BendDecay, BendRelease, PointCount };
    QPointF point(int which) const
    {
        const QRectF r = area();
        const double held = yOf(m_v.sustain);
        const double xa = r.left() + span(m_v.attack), xh = xa + (hasHold ? span(m_v.hold) : 0), xd = xh + span(m_v.decay), xs = r.right() - slot();
        const double low = hasFinal ? m_v.finalLevel : 0;
        switch (which) {
        case AttackTop: return {xa, yOf(1)};
        case HoldEnd: return {xh, yOf(1)};
        case DecayEnd: return {xd, held};
        case ReleaseEnd: return {xs + span(m_v.release), yOf(low)};
        case BendAttack: return {(r.left() + xa) / 2, yOf(along(0, 0.5, low, 1))};
        case BendDecay: return {(xh + xd) / 2, yOf(along(1, 0.5, 1, m_v.sustain))};
        default: return {xs + span(m_v.release) / 2, yOf(along(2, 0.5, m_v.sustain, low))};
        }
    }
    // Its gate, to see the envelope run on the curve as it is heard
    void setPlaying(bool on)
    {
        if (on) {
            m_stage = 1;
            m_time = 0;
            m_level = hasFinal ? m_v.finalLevel : 0;
            if (!m_timer) {
                m_timer = new QTimer(this);
                m_timer->setInterval(16);
                connect(m_timer, &QTimer::timeout, this, [this] { step(); });
            }
            m_clock.start();
            m_timer->start();
        } else if (m_stage) {
            m_stage = 5;
            m_time = 0;
            m_fallFrom = m_level;
        }
        update();
    }
    double playingLevel() const { return m_level; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = area();
        p.fillRect(rect(), QColor(24, 24, 28));
        p.setPen(QColor(50, 50, 56));
        p.drawLine(r.bottomLeft(), r.bottomRight());
        p.drawLine(r.topLeft(), r.topRight());
        const double low = hasFinal ? m_v.finalLevel : 0;
        const QPointF a = point(AttackTop), h = point(HoldEnd), d = point(DecayEnd), e = point(ReleaseEnd), hold(r.right() - slot(), d.y());
        QPainterPath line(QPointF(r.left(), yOf(low)));
        const auto segment = [&](int which, const QPointF &from, const QPointF &to, double levelFrom, double levelTo) {
            for (int k = 1; k <= 48; ++k) {
                const double u = k / 48.0;
                line.lineTo(from.x() + (to.x() - from.x()) * u, yOf(along(which, u, levelFrom, levelTo)));
            }
        };
        segment(0, QPointF(r.left(), yOf(low)), a, low, 1);
        line.lineTo(h);
        segment(1, h, d, 1, m_v.sustain);
        line.lineTo(hold);
        segment(2, hold, e, m_v.sustain, low);
        line.lineTo(r.right(), e.y());
        QPainterPath fill = line;
        fill.lineTo(r.bottomRight());
        fill.lineTo(r.bottomLeft());
        p.fillPath(fill, QColor(80, 200, 210, 55));
        p.setPen(QPen(QColor(120, 225, 235), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
        for (int k = 0; k < PointCount; ++k) {
            if (!shown(k)) continue;
            const bool bend = k >= BendAttack;
            p.setPen(QPen(bend ? QColor(190, 190, 196) : Qt::white, 1.5));
            p.setBrush(k == m_drag || k == m_over ? QColor(120, 225, 235) : QColor(30, 30, 34));
            p.drawEllipse(point(k), bend ? 4 : 5, bend ? 4 : 5);
        }
        if (m_stage) { // where it is now
            double x = hold.x();
            if (m_stage == 1) x = r.left() + (a.x() - r.left()) * std::min(1.0, m_time / std::max(0.001, m_v.attack));
            if (m_stage == 2) x = a.x() + (h.x() - a.x()) * std::min(1.0, m_time / std::max(0.001, m_v.hold));
            if (m_stage == 3) x = h.x() + (d.x() - h.x()) * std::min(1.0, m_time / std::max(0.001, m_v.decay));
            if (m_stage == 4) x = (d.x() + hold.x()) / 2;
            if (m_stage == 5) x = hold.x() + (e.x() - hold.x()) * std::min(1.0, m_time / std::max(0.001, m_v.release));
            const double y = yOf(m_level);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 60));
            p.drawEllipse(QPointF(x, y), 11, 11);
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(x, y), 5, 5);
        }
        p.setPen(QColor(170, 170, 176));
        QFont small = font();
        small.setPointSizeF(std::max(8.0, small.pointSizeF() - 2));
        p.setFont(small);
        QString text = QStringLiteral("A %1").arg(seconds(m_v.attack));
        if (hasHold) text += QStringLiteral("   H %1").arg(seconds(m_v.hold));
        text += QStringLiteral("   D %1   S %2   R %3").arg(seconds(m_v.decay)).arg(m_v.sustain, 0, 'f', 2).arg(seconds(m_v.release));
        p.drawText(rect().adjusted(6, 0, -6, -2), Qt::AlignBottom | Qt::AlignLeft, text);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        m_drag = nearest(e->position());
        update();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_drag < 0) {
            const int over = nearest(e->position());
            if (over != m_over) {
                m_over = over;
                update();
            }
            return;
        }
        const QRectF r = area();
        const QPointF q = e->position();
        const double level = std::clamp((r.bottom() - q.y()) / r.height(), 0.0, 1.0);
        const double low = hasFinal ? m_v.finalLevel : 0;
        // The bias that puts a segment's middle at this level
        const auto bias = [&](double from, double to, double current) {
            const double high = std::max(from, to), lower = std::min(from, to);
            if (high - lower < 1e-3) return current;
            const double y = std::clamp((level - lower) / (high - lower), 0.03, 0.97), x = 0.5;
            // y = x / ((1/b - 2)(1 - x) + 1), for b
            return std::clamp(1.0 / ((x / y - 1.0) / (1.0 - x) + 2.0), 0.03, 0.97);
        };
        switch (m_drag) {
        case AttackTop: m_v.attack = time(q.x() - r.left()); break;
        case HoldEnd: m_v.hold = std::max(0.0, time(q.x() - point(AttackTop).x())); break;
        case DecayEnd:
            m_v.decay = time(q.x() - point(HoldEnd).x());
            m_v.sustain = level;
            m_v.finalLevel = std::min(m_v.finalLevel, m_v.sustain); // never above it
            break;
        case ReleaseEnd:
            m_v.release = time(q.x() - (r.right() - slot()));
            if (hasFinal) m_v.finalLevel = std::min(level, m_v.sustain);
            break;
        case BendAttack: m_v.biasAttack = bias(low, 1, m_v.biasAttack); break;
        case BendDecay: m_v.biasDecay = bias(1, m_v.sustain, m_v.biasDecay); break;
        case BendRelease: m_v.biasRelease = bias(m_v.sustain, low, m_v.biasRelease); break;
        default: break;
        }
        update();
        if (edited) edited(m_v);
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        m_drag = -1;
        update();
    }
    void leaveEvent(QEvent *) override
    {
        m_over = -1;
        update();
    }

private:
    void step()
    {
        const double dt = m_clock.restart() * 0.001;
        m_time += dt;
        const double low = hasFinal ? m_v.finalLevel : 0;
        if (m_stage == 1) {
            const double u = m_time / std::max(0.001, m_v.attack);
            m_level = along(0, u, low, 1);
            if (u >= 1) {
                m_stage = hasHold && m_v.hold > 0 ? 2 : 3;
                m_time = 0;
            }
        } else if (m_stage == 2 && m_time >= m_v.hold) {
            m_stage = 3;
            m_time = 0;
        } else if (m_stage == 3) {
            const double u = m_time / std::max(0.001, m_v.decay);
            m_level = along(1, u, 1, m_v.sustain);
            if (u >= 1) m_stage = 4;
        } else if (m_stage == 4) {
            m_level = m_v.sustain;
        } else if (m_stage == 5) {
            const double u = m_time / std::max(0.001, m_v.release);
            m_level = along(2, u, m_fallFrom, low);
            if (u >= 1) {
                m_level = low;
                m_stage = 0;
                m_timer->stop();
            }
        }
        update();
    }
    bool shown(int which) const
    {
        if (which == HoldEnd) return hasHold;
        if (which >= BendAttack) return shape == Shape::Biased;
        return true;
    }
    QRectF area() const { return QRectF(rect()).adjusted(8, 10, -8, -22); }
    double yOf(double level) const { return area().bottom() - std::clamp(level, 0.0, 1.0) * area().height(); }
    double slot() const { return area().width() * (hasHold ? 0.23 : 0.3); } // each of them: what its time takes
    // The short times, those that count, get the room: the root of the time
    double span(double seconds) const { return slot() * std::sqrt(std::clamp(seconds / longest, 0.0, 1.0)); }
    double time(double pixels) const
    {
        const double u = std::clamp(pixels / std::max(1.0, slot()), 0.0, 1.0);
        return std::max(0.001, u * u * longest);
    }
    static QString seconds(double s) { return s < 1 ? QStringLiteral("%1 ms").arg(s * 1000, 0, 'f', 0) : QStringLiteral("%1 s").arg(s, 0, 'f', 2); }
    int nearest(const QPointF &q) const
    {
        int best = -1;
        double distance = 14;
        for (int k = 0; k < PointCount; ++k) {
            if (!shown(k)) continue;
            const QPointF d = point(k) - q;
            const double length = std::hypot(d.x(), d.y());
            if (length < distance) {
                distance = length;
                best = k;
            }
        }
        return best;
    }
    Values m_v;
    int m_drag = -1, m_over = -1;
    int m_stage = 0; // playing: 1 attack, 2 hold, 3 decay, 4 sustain, 5 release
    double m_level = 0, m_fallFrom = 1, m_time = 0;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
};
