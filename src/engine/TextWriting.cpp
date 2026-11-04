#include "TextWriting.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace {
constexpr float kNone = -1.f;
constexpr double kEdge = 2.5;      // pixels of the pen's soft edge
constexpr qint64 kMaxArea = 80'000'000; // beyond it, a text is written from left to right without working out its letters

// Zhang-Suen: thin to a line one pixel wide, keeping what is connected connected. `on` has a border of zeros
void thin(std::vector<uint8_t> &on, int w, int h)
{
    std::vector<int> candidates;
    for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x)
            if (on[size_t(y * w + x)]) candidates.push_back(y * w + x);
    std::vector<int> remove;
    for (bool changed = true; changed;) {
        changed = false;
        for (int pass = 0; pass < 2; ++pass) {
            remove.clear();
            for (int p : candidates) {
                if (!on[size_t(p)]) continue;
                const int n[8] = {on[size_t(p - w)],     on[size_t(p - w + 1)], on[size_t(p + 1)], on[size_t(p + w + 1)],
                                  on[size_t(p + w)],     on[size_t(p + w - 1)], on[size_t(p - 1)], on[size_t(p - w - 1)]};
                int count = 0, turns = 0;
                for (int k = 0; k < 8; ++k) {
                    count += n[k];
                    turns += !n[k] && n[(k + 1) % 8];
                }
                if (count < 2 || count > 6 || turns != 1) continue;
                const bool a = pass == 0 ? !(n[0] && n[2] && n[4]) : !(n[0] && n[2] && n[6]);
                const bool b = pass == 0 ? !(n[2] && n[4] && n[6]) : !(n[0] && n[4] && n[6]);
                if (a && b) remove.push_back(p);
            }
            for (int p : remove) on[size_t(p)] = 0;
            changed = changed || !remove.empty();
        }
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](int p) { return !on[size_t(p)]; }), candidates.end());
    }
}
} // namespace

std::shared_ptr<TextWriting> TextWriting::build(const QImage &inkImage, const QImage &fullImage, bool fullBottomUp, double lineStep)
{
    auto result = std::make_shared<TextWriting>();
    const QImage ink = inkImage.convertToFormat(QImage::Format_RGBA8888);
    QImage full = fullImage.convertToFormat(QImage::Format_RGBA8888);
    const int W = full.width(), H = full.height();
    result->m_width = W;
    result->m_height = H;
    result->m_bottomUp = fullBottomUp;
    const auto fullAlpha = [&](int x, int y) { return full.constScanLine(fullBottomUp ? H - 1 - y : y)[x * 4 + 3]; };

    // What holds a picture, with a border of one pixel
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (fullAlpha(x, y) || (x < ink.width() && y < ink.height() && ink.constScanLine(y)[x * 4 + 3])) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
    result->m_shown = full;
    if (x1 < 0) return result; // nothing to write
    const int w = x1 - x0 + 3, h = y1 - y0 + 3;
    const auto at = [&](int x, int y) { return (y - y0 + 1) * w + (x - x0 + 1); };

    std::vector<float> order;
    double length = 0;
    if (qint64(w) * h > kMaxArea) {
        // Too large to think about: the pen only goes from left to right
        order.assign(size_t(w) * size_t(h), kNone);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) order[size_t(at(x, y))] = float(x - x0);
        length = x1 - x0;
    } else {
        std::vector<uint8_t> strong(size_t(w) * size_t(h), 0), soft(size_t(w) * size_t(h), 0);
        for (int y = y0; y <= y1; ++y) {
            if (y >= ink.height()) break;
            const uchar *line = ink.constScanLine(y);
            for (int x = x0; x <= x1 && x < ink.width(); ++x) {
                const uchar a = line[x * 4 + 3];
                soft[size_t(at(x, y))] = a > 0;
                strong[size_t(at(x, y))] = a >= 96;
            }
        }
        // The pieces of ink, in the order of the reading: each line from left to right (a dot or an accent, which starts
        // after the word it is on starts, comes after it)
        struct Piece { std::vector<int> pixels; int left = std::numeric_limits<int>::max(), top = std::numeric_limits<int>::max(), bottom = -1; int line = 0; };
        std::vector<Piece> pieces;
        {
            std::vector<uint8_t> seen(size_t(w) * size_t(h), 0);
            std::vector<int> stack;
            for (int p = 0; p < w * h; ++p) {
                if (!strong[size_t(p)] || seen[size_t(p)]) continue;
                Piece piece;
                stack = {p};
                seen[size_t(p)] = 1;
                while (!stack.empty()) {
                    const int q = stack.back();
                    stack.pop_back();
                    piece.pixels.push_back(q);
                    piece.left = std::min(piece.left, q % w);
                    piece.top = std::min(piece.top, q / w);
                    piece.bottom = std::max(piece.bottom, q / w);
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int n = q + dy * w + dx;
                            if (n >= 0 && n < w * h && strong[size_t(n)] && !seen[size_t(n)]) {
                                seen[size_t(n)] = 1;
                                stack.push_back(n);
                            }
                        }
                }
                pieces.push_back(std::move(piece));
            }
        }
        int inkTop = std::numeric_limits<int>::max();
        for (const Piece &piece : pieces) inkTop = std::min(inkTop, piece.top);
        const double step = std::max(1.0, lineStep);
        for (Piece &piece : pieces) piece.line = int(((piece.top + piece.bottom) / 2.0 - inkTop) / step);
        std::stable_sort(pieces.begin(), pieces.end(), [](const Piece &a, const Piece &b) { return a.line != b.line ? a.line < b.line : a.left < b.left; });

        // The pen along the skeleton of each piece
        std::vector<uint8_t> line = strong;
        thin(line, w, h);
        order.assign(size_t(w) * size_t(h), kNone);
        const int around[8] = {-w, -w + 1, 1, w + 1, w, w - 1, -1, -w - 1};
        std::vector<int> stamp(size_t(w) * size_t(h), 0);
        int stampNow = 0;
        // How far to the right a branch leads: the pen takes the one that stays nearest first, then moves on
        const auto reach = [&](int from, int current) {
            ++stampNow;
            stamp[size_t(from)] = stampNow;
            stamp[size_t(current)] = stampNow;
            std::vector<int> next{from};
            int furthest = from % w, count = 0;
            while (!next.empty() && count++ < 4000) {
                const int q = next.back();
                next.pop_back();
                furthest = std::max(furthest, q % w);
                for (int d : around) {
                    const int n = q + d;
                    if (line[size_t(n)] && order[size_t(n)] == kNone && stamp[size_t(n)] != stampNow) {
                        stamp[size_t(n)] = stampNow;
                        next.push_back(n);
                    }
                }
            }
            return furthest;
        };
        const double lift = std::max(4.0, step * 0.1); // the pen lifted between two pieces
        double travelled = 0;
        std::vector<int> walked; // the skeleton pixels, in the order of the pen
        for (const Piece &piece : pieces) {
            std::vector<int> ofPiece;
            for (int p : piece.pixels)
                if (line[size_t(p)]) ofPiece.push_back(p);
            if (ofPiece.empty()) ofPiece.push_back(piece.pixels.front()), line[size_t(piece.pixels.front())] = 1;
            std::stable_sort(ofPiece.begin(), ofPiece.end(), [&](int a, int b) { return a % w != b % w ? a % w < b % w : a / w < b / w; });
            for (int start : ofPiece) {
                if (order[size_t(start)] != kNone) continue;
                std::vector<int> stack{start};
                order[size_t(start)] = float(travelled);
                walked.push_back(start);
                int dx = 1, dy = 0;
                while (!stack.empty()) {
                    const int current = stack.back();
                    int candidates[8], count = 0;
                    for (int d : around)
                        if (line[size_t(current + d)] && order[size_t(current + d)] == kNone) candidates[count++] = current + d;
                    if (!count) {
                        stack.pop_back(); // the end of a stroke: back to where it left another one
                        continue;
                    }
                    int chosen = candidates[0];
                    if (count > 1) {
                        double best = std::numeric_limits<double>::max();
                        for (int k = 0; k < count; ++k) {
                            const int c = candidates[k];
                            const int stepX = c % w - current % w, stepY = c / w - current / w;
                            const double straight = (stepX * dx + stepY * dy) / std::hypot(double(stepX), double(stepY));
                            const double cost = reach(c, current) - straight * 3;
                            if (cost < best) {
                                best = cost;
                                chosen = c;
                            }
                        }
                    }
                    dx = chosen % w - current % w;
                    dy = chosen / w - current / w;
                    travelled += std::hypot(double(dx), double(dy));
                    order[size_t(chosen)] = float(travelled);
                    walked.push_back(chosen);
                    // A pixel of the stair beside the step, which leads nowhere else: passed with it
                    for (int k = 0; k < count; ++k) {
                        const int c = candidates[k];
                        if (c == chosen || std::abs(c % w - chosen % w) > 1 || std::abs(c / w - chosen / w) > 1) continue;
                        bool leads = false;
                        for (int d : around)
                            leads = leads || (c + d != chosen && line[size_t(c + d)] && order[size_t(c + d)] == kNone);
                        if (!leads) {
                            order[size_t(c)] = float(travelled);
                            walked.push_back(c);
                        }
                    }
                    stack.push_back(chosen);
                }
                travelled += lift;
            }
        }
        length = travelled;

        // From the skeleton to the edges of the letters, first inside the ink: a stroke fills from its middle
        struct Spread { int pixel, distance; };
        std::deque<Spread> queue;
        for (int p : walked) queue.push_back({p, 0});
        const auto spread = [&](auto &&allowed) {
            while (!queue.empty()) {
                const Spread s = queue.front();
                queue.pop_front();
                const float here = order[size_t(s.pixel)];
                for (int k = 0; k < 8; k += 2) {
                    const int n = s.pixel + around[k];
                    if (n < 0 || n >= w * h || order[size_t(n)] != kNone || !allowed(n)) continue;
                    order[size_t(n)] = here + 0.35f;
                    length = std::max(length, double(order[size_t(n)]));
                    queue.push_back({n, s.distance + 1});
                }
            }
        };
        spread([&](int n) { return soft[size_t(n)] != 0; });
        // Then what is around them: their outline, their shadow, with the ink nearest
        for (int p = 0; p < w * h; ++p)
            if (order[size_t(p)] != kNone) queue.push_back({p, 0});
        spread([&](int n) { return n % w > 0 && n % w < w - 1 && n / w > 0 && n / w < h - 1; });
    }

    // The pixels of the picture, by order
    result->m_pixels.reserve(size_t(w) * size_t(h) / 4);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const uint8_t alpha = fullAlpha(x, y);
            if (!alpha) continue;
            const float o = order[size_t(at(x, y))];
            const int row = fullBottomUp ? H - 1 - y : y;
            result->m_pixels.push_back({uint32_t(row * result->m_shown.bytesPerLine() + x * 4 + 3), o == kNone ? float(length) : o, alpha});
        }
    std::stable_sort(result->m_pixels.begin(), result->m_pixels.end(), [](const Pixel &a, const Pixel &b) { return a.order < b.order; });
    result->m_length = result->m_pixels.empty() ? 0.0 : std::max(length, double(result->m_pixels.back().order));
    uchar *bytes = result->m_shown.bits();
    for (const Pixel &p : result->m_pixels) bytes[p.index] = 0;
    result->m_head = -kEdge;
    return result;
}

double TextWriting::orderAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= m_width || y >= m_height) return -1;
    const uint32_t index = uint32_t((m_bottomUp ? m_height - 1 - y : y) * m_shown.bytesPerLine() + x * 4 + 3);
    for (const Pixel &p : m_pixels)
        if (p.index == index) return p.order;
    return -1;
}

void TextWriting::setProgress(double progress)
{
    const double head = std::clamp(progress, 0.0, 1.0) * (m_length + kEdge) - (progress > 0 ? 0.0 : kEdge);
    if (head == m_head) return;
    m_head = head;
    const auto firstFrom = [&](double order) {
        return size_t(std::lower_bound(m_pixels.begin(), m_pixels.end(), order, [](const Pixel &p, double v) { return p.order < v; }) - m_pixels.begin());
    };
    const size_t full = firstFrom(head - kEdge), end = progress >= 1 ? m_pixels.size() : firstFrom(head);
    uchar *bytes = m_shown.bits();
    // Only those whose state changes: between where the pen was and where it is
    for (size_t k = std::min(m_full, full), last = std::max(m_end, end); k < last; ++k) {
        const Pixel &p = m_pixels[k];
        const double shown = progress >= 1 ? 1.0 : std::clamp((head - p.order) / kEdge, 0.0, 1.0);
        bytes[p.index] = uint8_t(std::lround(p.alpha * shown));
    }
    m_full = full;
    m_end = end;
}
