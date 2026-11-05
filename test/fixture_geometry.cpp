#include "FixtureOverlay.h"
#include <QCoreApplication>
#include <QLineF>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    FixturePatch patch;
    Fixture f;
    f.id = 1;
    f.center = {.5, .5};
    f.size = {.4, .1};
    patch.fixtures.push_back(f);
    const QSize composition(100, 100);
    const auto toWidget = [](QPointF n) { return QPointF(24 + 552 * n.x(), 24 + 552 * n.y()); };
    int failures = 0;
    auto check = [&](bool ok, const char *message) {
        if (!ok) { std::printf("FAIL fixture geometry: %s\n", message); ++failures; }
    };
    const auto corners = fixtureCorners(f, composition);
    const auto polygon = fixtureview::outline(f, composition, toWidget);
    for (int k = 0; k < 4; ++k)
        std::printf("corner %d: normalized=(%.9f,%.9f) widget=(%.9f,%.9f)\n", k,
                    corners[k].x(), corners[k].y(), polygon[k].x(), polygon[k].y());
    check(QLineF(corners[0], QPointF(.3, .45)).length() < 1e-9, "normalized top left");
    check(QLineF(corners[2], QPointF(.7, .55)).length() < 1e-9, "normalized bottom right");
    check(polygon.containsPoint(toWidget(f.center), Qt::OddEvenFill), "polygon contains its center");
    check(fixtureview::hit(patch, composition, toWidget, toWidget(f.center)) == 1, "center picks rectangle");
    check(fixtureview::hit(patch, composition, toWidget, toWidget({.1, .1})) == 0, "outside misses rectangle");
    patch.fixtures[0].rotation = 90;
    check(fixtureview::hit(patch, composition, toWidget, toWidget(f.center)) == 1, "center picks rotated rectangle");
    return failures ? 1 : 0;
}
