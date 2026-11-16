#pragma once
// A 3D scene through QRhi (render thread, one QRhi): the port of Scene3D (Model3D.h), step by step. For now the views
// for placing things (Scene3D::drawView): the models drawn flat with their edges (Simple) or with their colours and
// pictures as they are (Unlit), from the view's camera, into a picture of their own. Lit scenes (lights, shadows,
// environment, effects) are the next steps. Model pictures laid on models by a layer, displacement pictures and the
// textures of shared materials are not taken yet.

#include "Model3D.h"

#include <memory>
#include <vector>

class QRhi;
class QRhiCommandBuffer;
class QRhiResourceUpdateBatch;
class QRhiTexture;

class RhiScene
{
public:
    explicit RhiScene(QRhi *rhi);
    ~RhiScene();
    RhiScene(const RhiScene &) = delete;
    RhiScene &operator=(const RhiScene &) = delete;

    // The view's picture, w × h, RGBA8, premultiplied, the bottom row first (as every picture here); recorded in `cb`
    // (outside any pass) with `u` carrying the uploads of the frame (models' buffers and maps included). nullptr: nothing
    QRhiTexture *drawView(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *u, const SceneSettings &s,
                          const std::vector<Scene3D::Item> &items, int w, int h, Scene3D::ViewLook look);
    // A lit scene (Scene3D::draw's port, the first step): its models under its lights (directional, point, spot, area) and
    // the studio's light, physically based, antialiased as `quality` asks, then graded (colours, vignette). Same picture
    // conventions as drawView. Not yet: shadows, an environment, screen-space occlusion and reflections, glow and fog.
    // Pictures laid on models by a layer and displacement pictures are not taken yet
    QRhiTexture *draw(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *u, const SceneSettings &s, const std::vector<Scene3D::Item> &items,
                      const std::vector<Scene3D::Lamp> &lamps, int w, int h, const Scene3DQuality &quality = {});
    QRhi *rhiOwner() const; // the QRhi it draws with
    // Triangles it drew in its last frame
    int triangles() const { return m_triangles; }

private:
    std::vector<Scene3D::Item> withMaterials(QRhiResourceUpdateBatch *u, const std::vector<Scene3D::Item> &items);
    struct Impl;
    std::unique_ptr<Impl> d;
    int m_triangles = 0;
};
