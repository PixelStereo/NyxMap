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
// mapped meshes, the project's antialiasing and mipmaps.
// 3D scenes, lit by their lights and the studio (no shadows, environment, occlusion, reflections, glow or fog yet), and the
// interface's 3D views (flat and unlit). Not yet said once in the log: live pictures on models.
// Shown: output windows through a swapchain each (present); the interface's previews, the pixel readers and the
// published viewports as pixels (setPreviewRequests).

#include "Model3D.h"
#include "RhiTarget.h"

#include <QByteArray>
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
class QWindow;
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
    // An HDR file's unclipped pixels (RGBA floats, the bottom row first), as the environment of a scene takes them
    void hdrArrived(quint64 layer, const std::vector<float> &pixels, QSize size);
    // The frame: every layer's picture, the viewports' and the composition's (render thread, under the lock)
    void render(const IsfRenderContext &rc);
    // The interface's previews while it still shows them through OpenGL: reduced copies of pictures, read back during the
    // frame (render thread). Set before render(); taken after it. `slot`: the engine's preview slot; `id`: a layer, a
    // viewport, or 0 for the composition. Pixels: RGBA8, the picture's bottom row first (as an OpenGL texture holds them)
    struct PreviewRequest {
        int slot = 0;
        quint64 id = 0;
        int maxSide = 1024;
        enum Tap { Final, PreFx, Raw } tap = Final; // a layer's picture: finished, before its effects, as its source gave it
        QSize exact;                                 // when set: this size, whatever the aspect (a mean over a few texels)
    };
    struct PreviewPicture {
        int slot = 0;
        quint64 id = 0;
        QSize size;
        QByteArray pixels;
    };
    void setPreviewRequests(std::vector<PreviewRequest> requests);
    // The interface's 3D views (a cell showing a scene): drawn from the view's camera, as pixels like the other previews
    // (a PreviewPicture of that slot). Set before render()
    struct SceneViewRequest {
        int slot = 0;
        quint64 id = 0;
        SceneSettings view;
        QSize size;
        Scene3D::ViewLook look = Scene3D::ViewLook::Simple;
        std::vector<Scene3D::Item> items;
    };
    void setSceneViews(std::vector<SceneViewRequest> views);
    std::vector<PreviewPicture> takePreviews();
    // The viewport's picture into the window, through a swapchain of its own (render thread): true once shown.
    // `waitForDisplay`: this window is the one the frame follows the screen's refresh by (the others do not wait)
    bool present(quint64 viewport, QWindow *window, bool waitForDisplay);
    void releaseOutputs(QWindow *window); // before the window goes (any, or all: nullptr)
    // Tests: what the last present drew, top row first (needs setCapturePresented, set before the window's first frame)
    void setCapturePresented(bool on);
    QImage lastPresented(quint64 viewport) const;
    // The last pictures, top row first (render thread)
    QImage grabViewport(quint64 viewport);
    QImage grabOutput();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
