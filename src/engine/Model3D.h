#pragma once
// A 3D model as a layer's source: an FBX, glTF / GLB or OBJ file read (ufbx, cgltf), drawn into the layer's picture with
// physically based materials (base colour, metal, roughness, emission, the base colour's texture) under up to four
// lights, seen by a camera turning around a target. Everything that moves is a value of the layer (Model3D settings,
// LayerParams: model/…, camera/…, light/<n>/…), animated, recalled and driven as any other.
//
// The file is read once (Model3D::load, any thread); its meshes reach the GPU the first time it is drawn, on the
// render thread (draw). The meshes are drawn as the file places them (its nodes' transforms), centered and brought to
// a unit size when "fit" is on, then moved, turned and scaled as the settings say.

#include "Gl.h"

#include <QImage>
#include <QString>
#include <QVector3D>
#include <array>
#include <memory>
#include <vector>

struct ModelSettings {
    static constexpr int kLights = 4;
    // The model: centered and brought to a unit size (fit), then placed
    bool fit = true;
    std::array<float, 3> position{{0, 0, 0}};
    std::array<float, 3> rotation{{0, 0, 0}}; // degrees, around X, Y then Z
    float scale = 1;
    // The camera: around its target, at a distance, turned (yaw) and raised (pitch), its field of view
    std::array<float, 3> target{{0, 0, 0}};
    float distance = 3, yaw = 30, pitch = 15, fov = 45;
    // The light that is everywhere, the background (transparent by default: the layers under it show), the exposure
    std::array<float, 3> ambient{{1, 1, 1}};
    float ambientIntensity = 0.15f;
    std::array<float, 4> background{{0, 0, 0, 0}};
    float exposure = 1;
    struct Light {
        bool enabled = false;
        int type = 1; // 0 directional, 1 point, 2 spot
        std::array<float, 3> color{{1, 1, 1}};
        float intensity = 10;
        std::array<float, 3> position{{2, 3, 3}};
        float yaw = 0, pitch = -45; // where a directional light or a spot points (degrees)
        float angle = 30;           // a spot's half angle (degrees)
    };
    std::array<Light, kLights> lights;
    ModelSettings();
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

    // Draws it with these settings into its own target (w × h, with a depth buffer); its colour texture. Render
    // thread, a GL context current
    GLuint draw(const ModelSettings &s, int w, int h);
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
    GLuint m_program = 0;
    GLuint m_fbo = 0, m_color = 0, m_depth = 0;
    int m_w = 0, m_h = 0;
};
