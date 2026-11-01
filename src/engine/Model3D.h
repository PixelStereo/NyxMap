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
    float scale = 1;                          // all of it, times its scale along each axis
    std::array<float, 3> axisScale{{1, 1, 1}};
    float bend = 0;      // degrees: curved around an upright axis, across its width (a plane becomes a curved screen)
    bool unlit = false;  // its picture as it is (a screen), not lit as a material
    int detail = 1;      // a primitive's mesh: how finely it is cut (1 to 8)
    // Displacement: its points pushed along their normals by a layer's luminance (Layer::modelDisplace), times this,
    // a luminance of `displaceMid` staying in place
    float displaceAmount = 0.2f, displaceMid = 0;
    // A built-in primitive's material (a file keeps its own)
    std::array<float, 3> color{{0.8f, 0.8f, 0.8f}};
    float metal = 0, rough = 0.6f;
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
    // Its environment (a layer's picture, equirectangular: Layer::sceneEnvironment): how strong, turned around Y,
    // shown behind the models or only lighting them, how blurred when shown
    float envIntensity = 1, envRotation = 0, envBlur = 0;
    bool envShow = false;
};

class Model3D
{
public:
    Model3D();
    ~Model3D();
    Model3D(const Model3D &) = delete;
    Model3D &operator=(const Model3D &) = delete;

    static QStringList extensions(); // fbx, gltf, glb, obj
    static bool isModelFile(const QString &path); // a file of those, or a built-in primitive
    // Built-in primitives (no file): their virtual paths, /NyxMap/3D/<name>; centered, about a unit in size, unwrapped
    // (UV) for a picture to be laid on them
    static QStringList primitives(); // Cube, Open Cube, Plane, Disc, Cylinder, Open Cylinder, Cone, Pyramid, Sphere, Dome…
    static QString primitivePath(const QString &name);
    static bool isPrimitivePath(const QString &path);

    bool load(const QString &path, QString *err); // reads the file (no GL): its meshes, materials and textures
    QString path() const { return m_path; }
    int meshCount() const { return int(m_meshes.size()); }
    int triangleCount() const;

    // Its model matrix: fitted (if asked), then placed. Its meshes drawn with the scene's program bound (render thread)
    QMatrix4x4 matrix(const ModelPlacement &p) const;
    // Its meshes drawn with the scene's program bound (render thread); `picture`: a layer's picture over its UVs instead
    // of its materials' colours
    // pass 0: its opaque and cut-out meshes; 1: its blended ones (a picture laid on it: all of it in pass 0)
    void drawMeshes(GLuint program, GLuint picture = 0, int pass = 0, const ModelPlacement *look = nullptr);
    float width() const { return m_width; }     // across X, as read: what a bend curves
    bool isPrimitive() const { return m_primitive; }
    int detail() const { return m_detail; }
    void setDetail(int detail); // a primitive made again, finer or coarser (render thread)
    float centerX() const { return m_center.x(); }
    QVector3D center() const { return m_center; } // of its bounds, as read
    float radius() const { return m_radius; }     // half its bounds' diagonal, as read
    void releaseGl();

private:
    struct Vertex {
        float p[3], n[3], uv[2];
    };
    struct Material {
        std::array<float, 4> baseColor{{0.8f, 0.8f, 0.8f, 1}};
        float metallic = 0, roughness = 0.6f;
        std::array<float, 3> emissive{{0, 0, 0}};
        int texture = -1; // its base colour's, an index into m_images (-1: none)
        // Its other maps (indices into m_images): roughness and metal (in one of their channels), normals, occlusion,
        // emission
        int roughTex = -1, metalTex = -1, normalTex = -1, aoTex = -1, emTex = -1;
        int roughChannel = 1, metalChannel = 2; // glTF: green and blue of one picture
        float normalScale = 1, aoStrength = 1;
        int alphaMode = 0; // 0 opaque, 1 cut out under alphaCutoff, 2 blended (drawn after the others)
        float alphaCutoff = 0.5f;
    };
    struct Mesh {
        std::vector<Vertex> vertices;
        std::vector<quint32> indices;
        int material = 0;
        GLuint vao = 0, vbo = 0, ibo = 0;
    };
    bool loadPrimitive(const QString &name, QString *err);
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
    float m_radius = 1, m_width = 1;
    bool m_primitive = false;
    int m_detail = 1;
    void releaseMeshes();
    bool m_uploaded = false;
};

// Where a camera is and what it sees (aspect: width / height): the scene's renderer and the 3D view's overlays agree
struct Camera3D {
    QMatrix4x4 view, proj;
    QVector3D eye;
    static Camera3D of(const SceneSettings &s, float aspect);
    static QVector3D aim(float yawDeg, float pitchDeg); // the direction from these angles (a light, the camera's eye)
};

// Draws a scene: its models in one pass, with depth, under its lights, into its own target
class Scene3D
{
public:
    static constexpr int kMaxLights = 8;
    struct Item {
        std::shared_ptr<Model3D> model;
        ModelPlacement placement;
        GLuint picture = 0; // a layer's picture laid on it (straight alpha), 0: its materials
        GLuint displace = 0; // a layer's picture whose luminance pushes its points (0: none)
        float displaceAmount = 0, displaceMid = 0;
    };
    struct Lamp {
        LightSettings light;
        float gain = 1; // its layer's opacity
    };
    Scene3D() = default;
    Scene3D(const Scene3D &) = delete;
    Scene3D &operator=(const Scene3D &) = delete;
    // Its picture (premultiplied), w × h; render thread, a GL context current
    // environment: a layer's picture (equirectangular, its first row at the bottom), 0 none
    GLuint draw(const SceneSettings &s, const std::vector<Item> &items, const std::vector<Lamp> &lamps, int w, int h,
                GLuint environment = 0);
    void releaseGl();

private:
    bool copyEnvironment(GLuint source); // into m_env, blurred levels made (render thread)
    GLuint m_program = 0, m_sky = 0, m_skyVao = 0;
    bool m_compiled = false;
    GLuint m_fbo = 0, m_color = 0, m_depth = 0;
    GLuint m_env = 0, m_envFbo = 0, m_readFbo = 0; // the environment, RGBA16F with its blurred levels
    static constexpr int kEnvW = 1024, kEnvH = 512;
    int m_w = 0, m_h = 0;
};
