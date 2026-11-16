#pragma once
// The engine's frame through QRhi (render thread, under the engine's lock): the port of Render.cpp's renderPass,
// renderLayer, processLayer, compositeLayers, renderViewport and composite. It reads the same layers as the OpenGL
// renderer and keeps its own GPU state per layer (pictures, effect instances, meshes).
//
// Ported: video (RhiVideoTexture), image and ISF generator sources, a source's color space, ROI (rectangle,
// triangle, quad) and color, the effect chain (masks from a stage of the layer or another layer, mix, channels,
// mask-only effects), groups, viewports (plain or with their own picture, output color, opacity), the composition
// picture, composition opacity and blackout, blend modes (Difference reads a copy of what is drawn), surface and
// composition masks, mattes (Picture and Composition space), text, source transitions (a shader's, or a crossfade),
// mapped meshes.
// Not yet: live inputs, TouchEngine, web pages, 3D scenes, antialiasing, mipmaps (each said once in the log; the
// layer is drawn without it, or not at all).

#include "RhiTarget.h"

#include <QImage>
#include <QSize>
#include <map>
#include <memory>
#include <set>
#include <vector>

class Engine;
struct Layer;
struct IsfRenderContext;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiTexture;
class QRectF;
class QColor;

class RhiRenderer
{
public:
    explicit RhiRenderer(Engine &engine);
    ~RhiRenderer();
    RhiRenderer(const RhiRenderer &) = delete;
    RhiRenderer &operator=(const RhiRenderer &) = delete;

    bool init(QString *err); // makes the QRhi on the calling thread (the render thread from then on)
    QRhi *rhi() const;

    // An image layer's picture, as updateSource takes it (render thread, under the lock)
    void imageArrived(quint64 layer, const QImage &image);
    // The frame: every layer's picture, the viewports' and the composition's (render thread, under the lock)
    void render(const IsfRenderContext &rc);
    // The last pictures, top row first (render thread)
    QImage grabViewport(quint64 viewport);
    QImage grabOutput();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
