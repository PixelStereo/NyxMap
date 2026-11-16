#pragma once
// A viewport's test card: what lines a projector up and checks an output. Drawn at the viewport's own size:
//  - its name and size, large, in the middle;
//  - a grid of squares, the diagonals and a circle (a circle stays round when the aspect is right);
//  - a frame on the outermost pixels (nothing cropped), the four corners numbered and colored
//    (1 red top left, 2 green top right, 3 blue bottom right, 4 yellow bottom left: a flipped or turned picture
//    shows at once);
//  - color bars and a grey ramp in steps;
//  - motion, so that a frozen output is seen: a sweep turning in the circle and a block going to and fro.
// The still part is an image (here); its motion is drawn over it by the QRhi renderer (testcard.frag).

#include <QImage>
#include <QSize>
#include <QString>

class TestCard
{
public:
    // The still part of the card, top row first
    static QImage still(const QString &name, QSize size, const QString &outputName = {});
    // Where its moving parts are, in pixels from the bottom left (the sweep's circle: centre and radius; the block's band:
    // x0, x1, y0, y1)
    struct Motion {
        float size[2], circle[4], band[4];
    };
    static Motion motion(QSize size);
};
