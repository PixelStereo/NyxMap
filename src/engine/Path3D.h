#pragma once
#include <QStringList>
#include <QVector3D>
#include <vector>

// A Path: points laid out by a rule, as a SOP in TouchDesigner. A circle, a rectangle, a line or a spiral is
// also a path: a light or the camera can travel along it (along(), at an even pace). A Path is a 3D object of its
// scene: its place is its layer's (ModelPlacement), under its Merge 3Ds. Flat ones lie in X, Z.
struct Path3D {
    enum Kind { Circle, Rectangle, Line, Grid, Spiral, Sphere, Random, KindCount };
    int kind = Circle;
    int points = 32;          // along a circle, a line, a spiral; on a sphere; at random
    int columns = 6, rows = 6; // a grid
    float width = 4, depth = 4; // across X and Z (a circle's and a rectangle's size, a grid's, a line's length)
    float height = 0;          // a spiral's rise, the depth of a random box along Y
    float turns = 3;           // a spiral's
    int seed = 1;              // the random points'
    bool show = true;          // drawn in the 3D view (its points and lines)

    static QStringList kindNames(); // circle, rectangle, line, grid, spiral, sphere, random
    int pointCount() const;
    std::vector<QVector3D> pointsLocal() const; // in its own space
    // Its lines to draw, each a polyline (a closed one repeats its first point); none for scattered points
    std::vector<std::vector<QVector3D>> linesLocal() const;
    bool isPath() const { return kind == Circle || kind == Rectangle || kind == Line || kind == Spiral; }
    bool isClosed() const { return kind == Circle || kind == Rectangle; }
    // On its path at an even pace: 0 to 1 once around a closed one; an open one goes to its end at 1 and back at 2.
    // Its middle when it is not a path
    QVector3D along(double t) const;
};
