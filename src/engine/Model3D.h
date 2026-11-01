#pragma once
// 3D in NyxMap: a 3D Scene is a group whose picture is rendered, not composited. Its members are 3D objects — models
// (an FBX, glTF / GLB or OBJ file read with ufbx, cgltf) and lights — seen by the scene's camera turning around a
// target, under its ambient light. Each object is a layer of the list (enabled, soloed, animated, recalled and driven
// as any other); its values here are its parameters (LayerParams: model/…, light/…; the scene's camera/…, scene/…).
//
// A model's file is read once (Model3D::load, any thread); its meshes reach the GPU the first time it is drawn, on the
// render thread. Its meshes are drawn as the file places them (its nodes' transforms), centered and brought to a unit
// size when "fit" is on, then moved, turned and scaled as its placement says. The scene draws all its models in one
// pass with physically based materials (base colour, metal, roughness, emission, the base colour's texture) under its
// lights.

#include "Gl.h"

#include <QImage>
#include <QMatrix4x4>
#include <QString>
#include <QVector3D>
#include <array>
#include <memory>
#include <vector>

// Where a model is in its scene
struct ModelPlacement {
    bool fit = true; // centered and brought to a unit size first
    std::array<float, 3> position{{0, 0, 0}};
    std::array<float, 3> rotation{{0, 0, 0}}; // degrees, around X, Y then Z
    float scale = 1;
};

// A light of a scene
struct LightSettings {
    int type = 1; // 0 directional, 1 point, 2 spot
    std::array<float, 3> color{{1, 1, 1}};
    float intensity = 25;
    std::array<float, 3> position{{2, 3, 3}};
    float yaw = 0, pitch = -45; // where a directional light or a spot points (degrees)
    float angle = 30;           // a spot's half angle (degrees)
};

// A scene's camera, ambient light, background (transparent by default: the layers under it show) and exposure
struct SceneSettings {
    std::array<float, 3> target{{0, 0, 0}};
    float distance = 3, yaw = 30, pitch = 15, fov = 45;
    std::array<float, 3> ambient{{1, 1, 1}};
    float ambientIntensity = 0.15f;
    std::array<float, 4> background{{0, 0, 0, 0}};
    float exposure = 1;
};

class Model3D
{
public:
    Model3D();
    ~Model3D();
    Model3D(const Model3D &) = delete;
    Model3D &operator=(const Model3D &) = delete;

    static QStringList extensions(); // fbx, gltf, glb, obj
    static bool isModelFile(const QString &path);

    bool load(const QString &path, QString *err); // reads the file (no GL): its meshes, materials and textures
    QString path() const { return m_path; }
    int meshCount() const { return int(m_meshes.size()); }
    int triangleCount() const;

    // Its model matrix: fitted (if asked), then placed. Its meshes drawn with the scene's program bound (render thread)
    QMatrix4x4 matrix(const ModelPlacement &p) const;
    void drawMeshes(GLuint program);
    void releaseGl();

private:
    struct Vertex {
        float p[3], n[3], uv[2];
    };
    struct Material {
        std::array<float, 4> baseColor{{0.8f, 0.8f, 0.8f, 1}};
        float metallic = 0, roughness = 0.6f;
        std::array<float, 3> emissive{{0, 0, 0}};
        int texture = -1; // index into m_images (-1: none)
    };
    struct Mesh {
        std::vector<Vertex> vertices;
        std::vector<quint32> indices;
        int material = 0;
        GLuint vao = 0, vbo = 0, ibo = 0;
    };
    bool loadGltf(const QString &path, QString *err);
    bool loadUfbx(const QString &path, QString *err);
    int addImage(const QImage &image);
    void computeBounds();
    bool uploadGl();

    QString m_path;
    std::vector<Mesh> m_meshes;
    std::vector<Material> m_materials;
    std::vector<QImage> m_images;
    std::vector<GLuint> m_textures;
    QVector3D m_center;
    float m_radius = 1;
    bool m_uploaded = false;
};

// Draws a scene: its models in one pass, with depth, under its lights, into its own target
class Scene3D
{
public:
    static constexpr int kMaxLights = 8;
    struct Item {
        std::shared_ptr<Model3D> model;
        ModelPlacement placement;
    };
    struct Lamp {
        LightSettings light;
        float gain = 1; // its layer's opacity
    };
    Scene3D() = default;
    Scene3D(const Scene3D &) = delete;
    Scene3D &operator=(const Scene3D &) = delete;
    // Its picture (premultiplied), w × h; render thread, a GL context current
    GLuint draw(const SceneSettings &s, const std::vector<Item> &items, const std::vector<Lamp> &lamps, int w, int h);
    void releaseGl();

private:
    GLuint m_program = 0;
    bool m_compiled = false;
    GLuint m_fbo = 0, m_color = 0, m_depth = 0;
    int m_w = 0, m_h = 0;
};
