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
// lights, their shadows, and its environment or a studio's light.

#include <QtGlobal>

class QRhi;
class QRhiBuffer;
class QRhiResourceUpdateBatch;
class QRhiTexture;
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>

#include <QByteArray>
#include <QImage>
#include <QMatrix4x4>
#include <QString>
#include <QStringList>
#include <QVector3D>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <map>
#include <optional>
#include <vector>

enum MaterialMap { BaseColorMap, RoughnessMap, MetallicMap, NormalMap, OcclusionMap, EmissionMap, HeightMap, MaterialMapCount };
inline constexpr const char *materialMapNames[] = {"base_color", "roughness", "metallic", "normal", "occlusion", "emission", "height"};

// The 3D FX of a 3D object, listed in the inspector (none by default) and applied in the order they are listed. The shape
// FX (up to Noise) move the points of a model's mesh, in the shader; the placement FX place it afterwards: Transform 3D,
// Duplicate (a copy on each point of a Path) and Follow Path
enum class Fx3d { Displace, Bend, Twist, Taper, Wave, Noise, Transform, Duplicate, FollowPath };
inline constexpr int kFx3dCount = 9;
inline bool isShapeFx3d(Fx3d k) { return k <= Fx3d::Noise; }
QString fx3dKey(Fx3d k);                      // its name in a project: "displace", "bend", "follow_path"…
QString fx3dName(Fx3d k);                     // as the inspector says it: "Displace", "Follow Path"…
std::optional<Fx3d> fx3dFromKey(const QString &key);

// Where a model is in its scene
struct ModelPlacement {
    bool fit = true; // centered and brought to a unit size first
    std::array<float, 3> position{{0, 0, 0}};
    std::array<float, 3> rotation{{0, 0, 0}}; // degrees, around X, Y then Z
    float scale = 1;                          // all of it, times its scale along each axis
    std::array<float, 3> axisScale{{1, 1, 1}};
    // A Merge 3D's own transform, around its position: all it holds moves with it
    QMatrix4x4 transform() const;
    bool sharedMaterial = false;
    std::array<QRhiTexture *, MaterialMapCount> materialRhi{}; // the same maps as textures of a QRhi (its renderer makes them)
    std::array<float, 3> emission{};
    std::array<float, 2> uvTiling{{1, 1}};
    float normalStrength = 1, occlusionStrength = 1, ior = 1.5f, specular = 1;
    bool materialHeight = false;
    bool unlit = false;  // its picture as it is (a screen), not lit as a material
    int detail = 1;      // a primitive's mesh: how finely it is cut (1, the least, to 8)
    int shading = 0;     // 0 as it is made (a sphere smooth, a cube with its edges), 1 smooth, 2 flat: its facets show
    // What it takes for its shape: a shape FX (a bend, a displacement…) needs points to move, whatever `detail` says
    int meshDetail(bool shaped) const { return shaped ? std::max(detail, 6) : detail; }
    // A built-in primitive's material (a file keeps its own)
    std::array<float, 3> color{{0.8f, 0.8f, 0.8f}};
    float metal = 0, rough = 0.6f;
};

// The settings of one 3D FX (Fx3d): every kind reads its own. Lengths are in the units of the model's size (so any model
// reads the same), angles in degrees, phases in turns
//   Displace: amount, mid (a luminance staying in place), layer (the picture whose luminance pushes the points), stage
//   Bend: amount (° across the model's width: a plane becomes a curved screen)
//   Twist: angle (° from one end to the other), axis (0 X, 1 Y, 2 Z)
//   Taper: amount (the far end is 1 + amount times as wide, the near end 1 - amount), axis
//   Wave: amount (half the size), length (the size), phase, axis it travels along (it moves the points across it)
//   Noise: amount (half the size), scale (features per size), rate (changes of value per second, 0: still), interp (how
//          one value becomes the next: 0 not at all, a sharp change; 1 linearly; 2 smoothly), phase (steps), seed
//   Transform 3D: move, turn, size
//   Duplicate: layer (the Path), align, scale (of each copy), randomScale, randomRotation, seed
//   Follow Path: layer (the Path), position (0 to 1), speed (turns per second), orient
struct Fx3dSettings {
    float amount = 0, mid = 0, angle = 0, length = 0.5f, phase = 0, scale = 1, rate = 0;
    float randomScale = 0, randomRotation = 0, position = 0, speed = 0;
    int axis = 1, interp = 2, align = 1, seed = 1;
    bool orient = false;
    std::array<float, 3> move{{0, 0, 0}}, turn{{0, 0, 0}}, size{{1, 1, 1}};
    quint64 layer = 0;
    int stage = -1; // where that layer's picture is taken (as an FX mask's)
    static Fx3dSettings defaults(Fx3d kind);
    QMatrix4x4 transform() const; // Transform 3D: a position, a rotation and a size
};

// One 3D FX of a 3D object: several of a kind can follow each other. Its id is its own within the layer, and the address
// of its parameters ("fx3d/<id>/amount")
struct Fx3dEntry {
    quint64 id = 0;
    Fx3d kind = Fx3d::Bend;
    bool enabled = true;
    Fx3dSettings s;
    double travel = 0; // how far its speed or rate has taken it (Follow Path: turns; Noise: steps). Not kept in a project
};

struct Material3D {
    quint64 id = 0;
    QString name;
    std::array<float, 3> color{{.8f, .8f, .8f}};
    float metal = 0, rough = .6f;
    std::array<QString, MaterialMapCount> paths;
    std::array<QImage, MaterialMapCount> images;
    std::array<QString, MaterialMapCount> embeddedMapNames;
    std::array<QString, MaterialMapCount> embeddedMaps; // base64 PNG for imported maps without standalone paths
    std::array<float, 3> emissionColor{{1, 1, 1}};
    std::array<float, 2> uvTiling{{1, 1}};
    float emission = 0, normalStrength = 1, occlusionStrength = 1, height = 0, ior = 1.5f, specular = 1;
    quint64 picture = 0;
    int pictureStage = -1;
    bool setMap(MaterialMap slot, const QString &path, QString *error = nullptr);
    bool setEmbeddedMap(MaterialMap slot, const QImage &image, const QString &name);
    QJsonObject toJson(const QString &projectDir = {}) const {
        QJsonObject maps;
        for (int i = 0; i < MaterialMapCount; ++i) {
            if (paths[i].isEmpty()) {
                if (!embeddedMaps[i].isEmpty()) maps[materialMapNames[i]] = QJsonObject{{"png", embeddedMaps[i]}, {"name", embeddedMapNames[i]}};
                continue;
            }
            QJsonObject file{{"path", paths[i]}};
            if (!projectDir.isEmpty()) file["relative_path"] = QDir(projectDir).relativeFilePath(paths[i]);
            maps[materialMapNames[i]] = file;
        }
        return {{"id", QString::number(id)}, {"name", name},
                {"r", color[0]}, {"g", color[1]}, {"b", color[2]}, {"metal", metal}, {"rough", rough}, {"picture", QString::number(picture)}, {"picture_stage", pictureStage},
                {"maps", maps}, {"normal_strength", normalStrength}, {"occlusion_strength", occlusionStrength},
                {"emission", emission}, {"emission_r", emissionColor[0]}, {"emission_g", emissionColor[1]}, {"emission_b", emissionColor[2]},
                {"height", height}, {"ior", ior}, {"specular", specular}, {"tile_u", uvTiling[0]}, {"tile_v", uvTiling[1]}};
    }
    static Material3D fromJson(const QJsonObject &json, const QString &projectDir = {}) {
        Material3D material;
        material.id = json.value("id").toString().toULongLong();
        material.name = json.value("name").toString();
        for (int c = 0; c < 3; ++c)
            material.color[c] = float(std::clamp(json.value(QString(QChar("rgb"[c]))).toDouble(.8), 0., 1.));
        material.metal = float(std::clamp(json.value("metal").toDouble(), 0., 1.));
        material.rough = float(std::clamp(json.value("rough").toDouble(.6), 0., 1.));
        material.picture = json.value("picture").toString().toULongLong();
        material.pictureStage = json.value("picture_stage").toInt(-1);
        const QJsonObject maps = json.value("maps").toObject();
        for (int i = 0; i < MaterialMapCount; ++i) {
            const auto file = maps.value(materialMapNames[i]).toObject();
            QString path = file.value("path").toString();
            const QString relative = file.value("relative_path").toString();
            if (!projectDir.isEmpty() && !relative.isEmpty() && !QFileInfo(path).isFile()) {
                const QString alternate = QDir(projectDir).absoluteFilePath(relative);
                if (QFileInfo(alternate).isFile()) path = alternate;
            }
            if (!path.isEmpty() && !material.setMap(MaterialMap(i), path)) material.paths[i] = path;
            if (path.isEmpty()) {
                material.embeddedMaps[i] = file.value("png").toString();
                material.embeddedMapNames[i] = file.value("name").toString();
                if (!material.embeddedMaps[i].isEmpty())
                    material.images[i] = QImage::fromData(QByteArray::fromBase64(material.embeddedMaps[i].toLatin1()), "PNG");
            }
        }
        material.normalStrength = float(std::clamp(json.value("normal_strength").toDouble(1), 0., 10.));
        material.occlusionStrength = float(std::clamp(json.value("occlusion_strength").toDouble(1), 0., 1.));
        material.emission = float(std::clamp(json.value("emission").toDouble(), 0., 100.));
        material.emissionColor = {{float(std::clamp(json.value("emission_r").toDouble(1), 0., 1.)), float(std::clamp(json.value("emission_g").toDouble(1), 0., 1.)), float(std::clamp(json.value("emission_b").toDouble(1), 0., 1.))}};
        material.height = float(std::clamp(json.value("height").toDouble(), -2., 2.));
        material.ior = float(std::clamp(json.value("ior").toDouble(1.5), 1., 4.));
        material.specular = float(std::clamp(json.value("specular").toDouble(1), 0., 1.));
        material.uvTiling = {{float(std::clamp(json.value("tile_u").toDouble(1), .01, 100.)), float(std::clamp(json.value("tile_v").toDouble(1), .01, 100.))}};
        return material;
    }
};

// Import editable metallic-roughness surfaces without importing the model's geometry.
std::vector<Material3D> importGltfMaterials(const QString &path, QString *error = nullptr);

// A shared material's parameters on a placement (its maps are textures the QRhi renderer makes)
class MaterialTextureCache {
public:
    static void applyParameters(const Material3D &material, ModelPlacement &placement);
};

// A light of a scene
struct LightSettings {
    int type = 1; // 0 directional, 1 point, 2 spot, 3 area (a rectangle, lit on the side it faces)
    std::array<float, 3> color{{1, 1, 1}};
    float intensity = 25;
    std::array<float, 3> position{{2, 3, 3}};
    float yaw = 0, pitch = -45; // where a directional light or a spot points (degrees)
    float angle = 30;           // a spot's half angle (degrees)
    float falloff = 0.35f;      // a spot's soft edge: 0 sharp, 1 a gradient from its middle to its edge
    bool shadow = true;         // its light stopped by the models (the first ones of a scene: Scene3D::kMaxShadows)
    float softness = 2;         // how soft its shadows' edge is
    float width = 1, height = 1; // an area's size
    float rays = 1;              // how much it shows in a scene's fog
    // Where it points instead of its yaw and pitch: this point of the scene, or an object (Layer::lightLookAt)
    bool lookAt = false;
    std::array<float, 3> lookPoint{{0, 0, 0}};
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
    // Without an environment, a photo studio's light (panels over a dark room) for the models to reflect: how strong
    float studio = 1;
    // Darker where surfaces meet (screen space): how strong, how far it looks in the scene's units
    float ao = 1, aoRadius = 0.4f;
    float reflections = 1; // the other models seen in smooth surfaces (screen space)
    // As a camera: what shines more than its threshold glows around it, how far; the colours; darker edges
    float glow = 0.6f, glowThreshold = 1, glowSize = 0.6f;
    float contrast = 1, saturation = 1, temperature = 0, vignette = 0;
    // The air: how thick (0: none), up to where (it thins out above that height, by its falloff), how far it is seen, the
    // colour of what is lit evenly. The lights show in it, stopped by the models (LightSettings::rays)
    float fogDensity = 0, fogBase = 0, fogFalloff = 0.3f, fogReach = 30;
    std::array<float, 3> fogColor{{0.02f, 0.022f, 0.025f}};
};

// A camera object of a scene (Layer type Camera; its position is its layer's ModelPlacement): where it points, its
// field of view. A scene is seen through the one it chooses (Layer::activeCamera), or its own orbit camera
struct CameraSettings {
    float yaw = 30, pitch = -15; // where it points when it looks at nothing (degrees)
    float fov = 45;
    bool lookAt = true;                     // it looks at an object (Layer::lookAtObject) or at this point
    std::array<float, 3> lookPoint{{0, 0, 0}};
};

// What a frame of 3D may cost, the composition's for all its scenes: 0 off, 1 low, 2 medium, 3 high (antialiasing: off, 2×,
// 4×, 8×)
struct Scene3DQuality {
    int antialiasing = 2, shadows = 2, contact = 2, reflections = 2, glow = 2, atmosphere = 2;
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

    bool load(const QString &path, QString *err, const QStringList &hidden = {});
    const auto &objects() const { return m_objects; }
    const QStringList &hiddenObjects() const { return m_hidden; } // reads the file: its meshes, materials and textures
    QString path() const { return m_path; }
    int meshCount() const { return m_meshesRead; } // as read: drawn at one go by material
    int triangleCount() const;

    // Its model matrix: fitted (if asked), then placed
    QMatrix4x4 matrix(const ModelPlacement &p) const;
    bool hasBlended() const;
    float width() const { return m_width; }     // across X, as read: what a bend curves
    bool isPrimitive() const { return m_primitive; }
    int displacementUvTopology() const { return m_primitive ? m_displacementUvTopology : 0; }
    int detail() const { return m_detail; }
    void setDetail(int detail); // a primitive made again, finer or coarser (render thread)
    void setSmooth(bool on);    // its normals averaged where its faces meet, its edges rounded (render thread)
    float centerX() const { return m_center.x(); }
    QVector3D center() const { return m_center; } // of its bounds, as read
    QVector3D size() const { return m_size; }     // of its bounds, as read: what the shape FX measure themselves against
    float radius() const { return m_radius; }     // half its bounds' diagonal, as read
    // QRhi (render thread): its meshes' buffers and its maps' textures, made the first time (and again after setSmooth or
    // setDetail); `u` carries the uploads. What to draw with: one entry per mesh, its material as the renderer takes it
    // (the primitive's or shared material's parameters when `look` is given; a layer's picture instead when `picture`).
    // The textures of a shared material and a layer's picture are not taken yet
    struct RhiDraw {
        QRhiBuffer *vertices = nullptr, *indices = nullptr, *edges = nullptr;
        int indexCount = 0, edgeCount = 0;
        std::array<float, 4> baseColor{{0.8f, 0.8f, 0.8f, 1}};
        std::array<float, 3> emissive{{0, 0, 0}};
        float alphaCutoff = 0.5f;
        int alphaMode = 0; // 0 opaque, 1 cut out, 2 blended
        QRhiTexture *baseTexture = nullptr;
        bool blended = false; // drawn with the blended ones, after the others (a picture laid on it is drawn with the rest)
        // The rest of its material (the lit look): its maps' textures are roughness, metal, normals, occlusion, emission
        float metallic = 0, roughness = 0.6f, dielectric = 0.04f, normalScale = 1, aoStrength = 1;
        std::array<float, 4> roughChannel{{0, 1, 0, 0}}, metalChannel{{0, 0, 1, 0}};
        std::array<QRhiTexture *, 5> maps{};
    };
    std::vector<RhiDraw> rhiDraws(QRhi *rhi, QRhiResourceUpdateBatch *u, const ModelPlacement *look, bool picture, bool edges);
    void releaseRhi();

private:
    struct RhiData;
    std::unique_ptr<RhiData> m_rhi;
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
        QString object;
    };
    std::vector<quint32> edgeIndices(const Mesh &m) const;
    bool loadPrimitive(const QString &name, QString *err);
    bool loadGltf(const QString &path, QString *err);
    bool loadUfbx(const QString &path, QString *err);
    int addImage(const QImage &image);
    void computeBounds();
    void mergeMeshes();

    QString m_path;
    QStringList m_hidden;
    std::vector<std::pair<QString, QString>> m_objects;
    std::vector<Mesh> m_meshes;
    int m_meshesRead = 0;
    std::vector<Material> m_materials;
    std::vector<QImage> m_images;
    QVector3D m_center, m_size{1, 1, 1};
    float m_radius = 1, m_width = 1;
    bool m_primitive = false;
    int m_displacementUvTopology = 0;
    int m_detail = 1;
    bool m_smooth = false;
};

// Where a camera is and what it sees (aspect: width / height): the scene's renderer and the 3D view's overlays agree
struct Camera3D {
    QMatrix4x4 view, proj;
    QVector3D eye;
    float nearPlane = 0.01f, farPlane = 100;
    static Camera3D of(const SceneSettings &s, float aspect);
    static QVector3D aim(float yawDeg, float pitchDeg); // the direction from these angles (a light, the camera's eye)
    static void anglesOf(const QVector3D &direction, float *yawDeg, float *pitchDeg); // the reverse
};

// What a scene is drawn from (RhiScene.h): its models with their placements and its lamps
class Scene3D
{
public:
    static constexpr int kMaxShadows = 4;      // spots and directional lights
    static constexpr int kMaxPointShadows = 2; // point lights, all around
    struct Item {
        std::shared_ptr<Model3D> model;
        ModelPlacement placement;
        float displaceAmount = 0, displaceMid = 0;
        QMatrix4x4 parent;   // the Merge 3D it is in, all of them: applied after its placement
        // Its shape FX, in the order they move its points (Displace and Bend take their settings above, in the placement)
        struct ShapeOp {
            Fx3d kind = Fx3d::Bend;
            std::array<float, 4> a{}, b{}; // the settings of Bend, Twist, Taper, Wave and Noise, as the shader reads them
        };
        std::vector<ShapeOp> shape;
        // QRhi: the same pictures as textures of that QRhi (set by its renderer from the layers below), and which layers they are
        QRhiTexture *pictureRhi = nullptr, *displaceRhi = nullptr;
        std::shared_ptr<const Material3D> material; // the shared material it takes its maps from (QRhi makes its textures)
        quint64 pictureLayer = 0, displaceLayer = 0;
        int pictureStage = -1, displaceStage = -1;
    };
    struct Lamp {
        LightSettings light;
        float gain = 1; // its layer's opacity
    };
    // A view for placing things, nothing of the lights in it. Simple: flat colours and edges; Unlit: the models' colours
    // and pictures as they are
    enum class ViewLook { Simple, Unlit };
    // Without a graphics card (a renderer that runs on the processor; NYXMAP_3D_LITE to try it): what costs the most goes,
    // no antialiasing, shadows, contact shadows or reflections
    static bool lite(const QString &renderer);
};
