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

#include "Gl.h"
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
#include <vector>

enum MaterialMap { BaseColorMap, RoughnessMap, MetallicMap, NormalMap, OcclusionMap, EmissionMap, HeightMap, MaterialMapCount };
inline constexpr const char *materialMapNames[] = {"base_color", "roughness", "metallic", "normal", "occlusion", "emission", "height"};

// Where a model is in its scene
struct ModelPlacement {
    bool fit = true; // centered and brought to a unit size first
    std::array<float, 3> position{{0, 0, 0}};
    std::array<float, 3> rotation{{0, 0, 0}}; // degrees, around X, Y then Z
    float scale = 1;                          // all of it, times its scale along each axis
    std::array<float, 3> axisScale{{1, 1, 1}};
    // A Merge 3D's own transform, around its position: all it holds moves with it
    QMatrix4x4 transform() const;
    float bend = 0;      // degrees: curved around an upright axis, across its width (a plane becomes a curved screen)
    bool sharedMaterial = false;
    std::array<GLuint, MaterialMapCount> materialTextures{};
    std::array<float, 3> emission{};
    std::array<float, 2> uvTiling{{1, 1}};
    float normalStrength = 1, occlusionStrength = 1, ior = 1.5f, specular = 1;
    bool materialHeight = false;
    bool unlit = false;  // its picture as it is (a screen), not lit as a material
    int detail = 1;      // a primitive's mesh: how finely it is cut (1, the least, to 8)
    int shading = 0;     // 0 as it is made (a sphere smooth, a cube with its edges), 1 smooth, 2 flat: its facets show
    // What it takes for its shape: a bend or a displacement need points to move, whatever `detail` says
    int meshDetail(bool displaced) const { return bend != 0 || displaced ? std::max(detail, 6) : detail; }
    // Displacement: its points pushed along their normals by a layer's luminance (Layer::modelDisplace), times this,
    // a luminance of `displaceMid` staying in place
    float displaceAmount = 0.2f, displaceMid = 0;
    // A built-in primitive's material (a file keeps its own)
    std::array<float, 3> color{{0.8f, 0.8f, 0.8f}};
    float metal = 0, rough = 0.6f;
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

// One cache per GL owner. A shared material uploads each image once even when used by multiple objects.
class MaterialTextureCache {
public:
    void apply(const Material3D &material, ModelPlacement &placement);
    void advance();
    void release();
private:
    struct Entry { Texture2D texture; quint64 used = 0; };
    std::map<qint64, Entry> m_textures;
    quint64 m_frame = 0;
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
    // It goes around a shape whose middle is its position: flat (X, Z) as it is made, then turned, a size across each way
    int pathShape = 0; // 0 none, 1 circle, 2 rectangle
    float pathWidth = 4, pathDepth = 4;
    std::array<float, 3> pathRotation{{0, 0, 0}}; // degrees, around X, Y then Z
    float pathPosition = 0, pathSpeed = 0;        // where on it (0 to 1, once around), turns per second
    double pathTravel = 0;                        // how far its speed has taken it (not kept in a project)
    QVector3D pathPoint(double around) const;     // on its path, 0 to 1 once around at an even pace
    QVector3D located() const;                    // where it is now, in its Merge 3Ds' space
    std::vector<QVector3D> pathOutline() const;   // its path to draw, closed (none: empty)
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

// What a frame of 3D may cost, the composition's for all its scenes: 0 off, 1 low, 2 medium, 3 high (antialiasing: off, 2×,
// 4×, 8×)
struct Scene3DQuality {
    int antialiasing = 2, shadows = 2, contact = 2, reflections = 2, glow = 2, atmosphere = 2;
};

// Where the program that shades a mesh takes what its material says, found once
struct MeshUniforms {
    GLint dielectric = -1;
    GLint base = -1, metal = -1, rough = -1, emissive = -1, alphaMode = -1, alphaCutoff = -1, normalScale = -1, aoStrength = -1;
    GLint roughCh = -1, metalCh = -1, hasTex = -1, layerTex = -1, has[5] = {-1, -1, -1, -1, -1};
    void resolve(GLuint program);
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
    const QStringList &hiddenObjects() const { return m_hidden; } // reads the file (no GL): its meshes, materials and textures
    QString path() const { return m_path; }
    int meshCount() const { return m_meshesRead; } // as read: drawn at one go by material
    int triangleCount() const;

    // Its model matrix: fitted (if asked), then placed. Its meshes drawn with the scene's program bound (render thread)
    QMatrix4x4 matrix(const ModelPlacement &p) const;
    // Its meshes drawn with the scene's program bound (render thread); `picture`: a layer's picture over its UVs instead
    // of its materials' colours
    // pass 0: its opaque and cut-out meshes; 1: its blended ones (a picture laid on it: all of it in pass 0)
    void drawMeshes(const MeshUniforms &uniforms, GLuint picture = 0, int pass = 0, const ModelPlacement *look = nullptr);
    void drawDepth(bool picture); // only where it is: no colour is computed (shadows, depth)
    void drawEdges(); // only its edges, as lines (made when first asked)
    bool hasBlended() const;
    float width() const { return m_width; }     // across X, as read: what a bend curves
    bool isPrimitive() const { return m_primitive; }
    int displacementUvTopology() const { return m_primitive ? m_displacementUvTopology : 0; }
    int detail() const { return m_detail; }
    void setDetail(int detail); // a primitive made again, finer or coarser (render thread)
    void setSmooth(bool on);    // its normals averaged where its faces meet, its edges rounded (render thread)
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
        QString object;
        GLuint vao = 0, vbo = 0, ibo = 0;
        GLuint edgeVao = 0, edgeIbo = 0;
        GLsizei edgeCount = 0;
    };
    bool loadPrimitive(const QString &name, QString *err);
    bool loadGltf(const QString &path, QString *err);
    bool loadUfbx(const QString &path, QString *err);
    int addImage(const QImage &image);
    void computeBounds();
    void mergeMeshes();
    void makeEdges(Mesh &m);
    void upload();
    bool uploadGl();

    QString m_path;
    QStringList m_hidden;
    std::vector<std::pair<QString, QString>> m_objects;
    std::vector<Mesh> m_meshes;
    int m_meshesRead = 0;
    std::vector<Material> m_materials;
    std::vector<QImage> m_images;
    std::vector<GLuint> m_textures;
    QVector3D m_center;
    float m_radius = 1, m_width = 1;
    bool m_primitive = false;
    int m_displacementUvTopology = 0;
    int m_detail = 1;
    void releaseMeshes();
    bool m_uploaded = false;
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

// Draws a scene: its models in one pass, with depth, under its lights, into its own target
class Scene3D
{
public:
    static constexpr int kMaxLights = 8;
    static constexpr int kMaxShadows = 4;      // spots and directional lights
    static constexpr int kMaxPointShadows = 2; // point lights, all around
    struct Item {
        std::shared_ptr<Model3D> model;
        ModelPlacement placement;
        GLuint picture = 0; // a layer's picture laid on it (straight alpha), 0: its materials
        GLuint displace = 0; // a layer's picture whose luminance pushes its points (0: none)
        float displaceAmount = 0, displaceMid = 0;
        QMatrix4x4 parent;   // the Merge 3D it is in, all of them: applied after its placement
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
    // environmentLinear: its values linear light (an HDR file as read), else display encoded (a layer's picture)
    GLuint draw(const SceneSettings &s, const std::vector<Item> &items, const std::vector<Lamp> &lamps, int w, int h,
                GLuint environment = 0, bool environmentLinear = false, const Scene3DQuality &quality = {});
    // A view for placing things, nothing of the lights in it (its picture, w × h; render thread). Simple: flat colours and
    // edges; Unlit: the models' colours and pictures as they are
    enum class ViewLook { Simple, Unlit };
    GLuint drawView(const SceneSettings &s, const std::vector<Item> &items, int w, int h, ViewLook look = ViewLook::Simple);
    void releaseGl();
    int shadowRenders() const { return m_shadowRenders; } // how many times its shadow maps were drawn: when something moved
    int shadowMapsDrawn() const { return m_mapsDrawn; }   // and how many maps in all: a light moving draws only its own
    // Its last frame: the triangles it has, those it had to compute in all its passes (0: nothing moved, it was kept)
    int triangles() const { return m_triangles.load(); }
    int trianglesComputed() const { return m_computed.load(); }
    int framesDrawn() const { return m_framesDrawn; }
    // Without a graphics card (a renderer that runs on the processor; NYXMAP_3D_LITE to try it): what costs the most goes,
    // no antialiasing, shadows, contact shadows or reflections
    static bool lite(const QString &renderer);

private:
    bool copyEnvironment(GLuint source); // into m_env, blurred levels made (render thread)
    void makeLookTargets(int w, int h);
    void releaseLook();
    static constexpr int kGlowLevels = 6;
    GLuint m_glowDownProgram = 0, m_glowUpProgram = 0, m_fogProgram = 0, m_fogBlurProgram = 0, m_postProgram = 0, m_rawSampler = 0;
    GLuint m_msBright = 0, m_bright = 0, m_glowTex[kGlowLevels] = {}, m_fogTex[2] = {}, m_final = 0, m_lookFbo = 0;
    GLuint m_picture = 0; // what it gives: its final picture, or m_color with nothing over it
    // Where a program takes a model's place, found once
    struct PlaceUniforms {
        GLint model = -1, normal = -1, bend = -1, bendCx = -1, bendW = -1, hasDisp = -1, dispAmount = -1, dispMid = -1, uvTiling = -1, dispTopology = -1;
        void resolve(GLuint program);
    };
    GLuint m_plainProgram = 0, m_unlitProgram = 0, m_plainFbo = 0, m_plainColor = 0, m_plainDepth = 0;
    PlaceUniforms m_placePlain, m_placeUnlit;
    MeshUniforms m_unlitUniforms;
    MeshUniforms m_meshUniforms;
    PlaceUniforms m_placeMain, m_placeDepth, m_placePoint;
    struct SceneUniforms {
        GLint u_ambient = -1;
        GLint u_aoAmount = -1;
        GLint u_aoTex = -1;
        GLint u_color = -1;
        GLint u_cos = -1;
        GLint u_cosIn = -1;
        GLint u_count = -1;
        GLint u_depthRange = -1;
        GLint u_disp = -1;
        GLint u_env = -1;
        GLint u_envIntensity = -1;
        GLint u_envLinear = -1;
        GLint u_envMaxLod = -1;
        GLint u_envRot = -1;
        GLint u_exposure = -1;
        GLint u_eye = -1;
        GLint u_glowLimit = -1;
        GLint u_hasEnv = -1;
        GLint u_hasLast = -1;
        GLint u_last = -1;
        GLint u_lastExposure = -1;
        GLint u_ldir = -1;
        GLint u_lhalf = -1;
        GLint u_lpos = -1;
        GLint u_lright = -1;
        GLint u_lup = -1;
        GLint u_penumbra = -1;
        GLint u_pointFar = -1;
        GLint u_pointMap0 = -1;
        GLint u_pointMap1 = -1;
        GLint u_pointRaw0 = -1;
        GLint u_pointRaw1 = -1;
        GLint u_pointShadow = -1;
        GLint u_pointSoft = -1;
        GLint u_proj = -1;
        GLint u_reflections = -1;
        GLint u_screen = -1;
        GLint u_seen = -1;
        GLint u_shadow = -1;
        GLint u_shadowBias = -1;
        GLint u_shadowRadius = -1;
        GLint u_shadowRaw = -1;
        GLint u_shadows = -1;
        GLint u_steps = -1;
        GLint u_studio = -1;
        GLint u_taps = -1;
        GLint u_tex = -1;
        GLint u_type = -1;
        GLint u_view = -1;
        GLint u_viewProj = -1;
    } m_sceneUniforms;
    GLint m_unlitAt = -1, m_shadingAt = -1;
    GLuint m_program = 0, m_sky = 0, m_skyVao = 0;
    bool m_compiled = false;
    GLuint m_fbo = 0, m_color = 0;                          // its picture
    GLuint m_msFbo = 0, m_msColor = 0, m_depth = 0;         // drawn antialiased here
    GLuint m_shadowProgram = 0, m_shadowFbo = 0, m_shadowMaps = 0; // the lights' views of the models, one layer each
    GLuint m_pointProgram = 0, m_pointMaps[kMaxPointShadows] = {}; // a point light's: the distances all around it
    QByteArray m_shadowKey; // the models its maps show
    QMatrix4x4 m_shadowSeen[kMaxShadows]; // each map's light, as it was drawn
    bool m_shadowFresh[kMaxShadows] = {};
    QVector3D m_pointSeenAt[kMaxPointShadows];
    float m_pointSeenFar[kMaxPointShadows] = {};
    bool m_pointFresh[kMaxPointShadows] = {};
    int m_shadowRenders = 0, m_mapsDrawn = 0;
    GLuint m_preFbo = 0, m_preDepth = 0;                    // the depth of what is seen
    GLuint m_aoProgram = 0, m_aoBlurProgram = 0, m_aoFbo = 0, m_aoTex[2] = {}; // the occlusion it makes, half its size, blurred
    bool m_lite = false;
    std::atomic<int> m_triangles{0}, m_computed{0};
    int m_framesDrawn = 0, m_stillFrames = 0;
    QByteArray m_frameKey;    // what its picture shows: while it does not change, it is kept
    GLuint m_envSource = 0;   // an HDR file's picture copied into m_env: it does not change
    int m_samples = 0, m_shadowSize = 0; // of its targets and its shadow maps, as its quality asks
    bool m_hasPicture = false;                              // m_color holds the last frame: what a reflection shows
    GLuint m_env = 0, m_envFbo = 0, m_readFbo = 0; // the environment, RGBA16F with its blurred levels
    static constexpr int kEnvW = 1024, kEnvH = 512;
    int m_w = 0, m_h = 0;
};
