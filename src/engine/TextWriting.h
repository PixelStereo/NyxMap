#pragma once
// A text written as by a pen, from any font: the letters are thinned down to their skeleton, the pen goes along it from
// left to right, each word after the other, and every pixel of the text (its outline and its shadow too) shows when the
// pen passes nearest to it. A font does not say in which order its letters are drawn: the order is worked out from their
// shapes, as a hand would, and it can differ from the real ductus at a crossing.
// Built on the render thread when the text or its style change; shown by setProgress at each frame, which only touches
// the pixels whose state changes.

#include <QImage>
#include <cstdint>
#include <memory>
#include <vector>

class TextWriting
{
public:
    // `ink`: the text alone, top row first; its alpha says where the letters are. `full`: the picture to reveal, of the
    // same size, in RGBA8888. `fullBottomUp`: it is stored bottom row first (as textures are). `lineStep`: the distance
    // between two lines of the text, in pixels
    static std::shared_ptr<TextWriting> build(const QImage &ink, const QImage &full, bool fullBottomUp, double lineStep);

    void setProgress(double progress); // 0: nothing, 1: all of it
    const QImage &image() const { return m_shown; }

    double length() const { return m_length; }     // the way of the pen, in pixels
    // Where the pen gets to this pixel (top row first), in pixels along its way; < 0: nothing is shown there
    double orderAt(int x, int y) const;

private:
    struct Pixel {
        uint32_t index; // in m_shown's bytes
        float order;
        uint8_t alpha;
    };
    std::vector<Pixel> m_pixels; // by order
    QImage m_shown;
    double m_length = 0;
    double m_head = -1;          // where the pen is
    size_t m_full = 0, m_end = 0; // the pixels before m_full are whole, those from m_end not there yet
    int m_width = 0, m_height = 0;
    bool m_bottomUp = false;
};
