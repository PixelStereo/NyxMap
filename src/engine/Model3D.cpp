#include "Model3D.h"

#include <QtGlobal>
#include <utility>
#include <map>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "ufbx.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMatrix4x4>
#include <QtMath>
#include <rhi/qrhi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

Model3D::Model3D() = default;
Model3D::~Model3D() = default; // its GPU objects go with releaseRhi, on the render thread

QStringList Model3D::extensions() { return {QStringLiteral("fbx"), QStringLiteral("gltf"), QStringLiteral("glb"), QStringLiteral("obj")}; }

bool Model3D::isModelFile(const QString &path) { return isPrimitivePath(path) || extensions().contains(QFileInfo(path).suffix().toLower()); }

static const QString kPrimitiveFolder = QStringLiteral("/NyxMap/3D/");

QStringList Model3D::primitives()
{
    return {QStringLiteral("Cube"),     QStringLiteral("Open Cube"), QStringLiteral("Plane"),         QStringLiteral("Disc"),
            QStringLiteral("Cylinder"), QStringLiteral("Open Cylinder"), QStringLiteral("Cone"),      QStringLiteral("Pyramid"),
            QStringLiteral("Sphere"),   QStringLiteral("Dome"),      QStringLiteral("Torus"),         QStringLiteral("Capsule")};
}

QString Model3D::primitivePath(const QString &name) { return kPrimitiveFolder + name; }

bool Model3D::isPrimitivePath(const QString &path)
{
    const QString p = QString(path).replace('\\', '/');
    const int at = p.lastIndexOf(kPrimitiveFolder);
    return at >= 0 && primitives().contains(p.mid(at + kPrimitiveFolder.size()));
}

int Model3D::triangleCount() const
{
    int n = 0;
    for (const Mesh &m : m_meshes) n += int(m.indices.size() / 3);
    return n;
}

int Model3D::addImage(const QImage &image)
{
    if (image.isNull()) return -1;
    m_images.push_back(image.convertToFormat(QImage::Format_RGBA8888));
    return int(m_images.size()) - 1;
}

bool Model3D::load(const QString &path, QString *err, const QStringList &hidden)
{
    ++m_revision;
    m_path = path;
    m_hidden = hidden;
    m_objects.clear();
    m_meshes.clear();
    m_materials.clear();
    m_images.clear();
    const QString ext = QFileInfo(path).suffix().toLower();
    m_primitive = isPrimitivePath(path);
    const bool ok = m_primitive                                                    ? loadPrimitive(path.section('/', -1), err)
                    : ext == QLatin1String("gltf") || ext == QLatin1String("glb") ? loadGltf(path, err)
                                                                                  : loadUfbx(path, err);
    if (!ok) return false;
    if (m_meshes.empty()) {
        if (err) *err = QStringLiteral("No mesh in this file.");
        return false;
    }
    if (m_materials.empty()) m_materials.emplace_back();
    m_meshesRead = int(m_meshes.size());
    computeBounds(); // hiding geometry must not change the fit or pivot
    if (!m_hidden.isEmpty())
        m_meshes.erase(std::remove_if(m_meshes.begin(), m_meshes.end(), [this](const Mesh &m) { return m_hidden.contains(m.object); }), m_meshes.end());
    mergeMeshes();
    return true;
}

// One drawing per material: a file of thousands of parts costs as one of a few
void Model3D::mergeMeshes()
{
    std::vector<Mesh> merged;
    std::map<int, size_t> ofMaterial;
    for (Mesh &m : m_meshes) {
        const auto found = ofMaterial.find(m.material);
        if (found == ofMaterial.end()) {
            ofMaterial[m.material] = merged.size();
            merged.push_back(std::move(m));
            continue;
        }
        Mesh &into = merged[found->second];
        const quint32 first = quint32(into.vertices.size());
        into.vertices.insert(into.vertices.end(), m.vertices.begin(), m.vertices.end());
        into.indices.reserve(into.indices.size() + m.indices.size());
        for (quint32 index : m.indices) into.indices.push_back(first + index);
    }
    m_meshes = std::move(merged);
}

void Model3D::computeBounds()
{
    QVector3D lo(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    QVector3D hi = -lo;
    for (const Mesh &m : m_meshes)
        for (const Vertex &v : m.vertices) {
            const QVector3D p(v.p[0], v.p[1], v.p[2]);
            lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
            hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
        }
    m_center = (lo + hi) / 2;
    m_size = QVector3D(std::max(1e-6f, hi.x() - lo.x()), std::max(1e-6f, hi.y() - lo.y()), std::max(1e-6f, hi.z() - lo.z()));
    m_radius = std::max(1e-6f, (hi - lo).length() / 2);
    m_width = std::max(1e-6f, hi.x() - lo.x());
}

Fx3dSettings Fx3dSettings::defaults(Fx3d kind)
{
    Fx3dSettings d;
    switch (kind) {
    case Fx3d::Displace: d.amount = 0.2f; break;
    case Fx3d::Bend: d.amount = 45; break;
    case Fx3d::Twist: d.angle = 90; d.axis = 1; break;
    case Fx3d::Taper: d.amount = 0.5f; d.axis = 1; break;
    case Fx3d::Wave: d.amount = 0.1f; d.length = 0.5f; d.axis = 0; break;
    case Fx3d::Noise: d.amount = 0.1f; d.scale = 2; d.rate = 1; d.interp = 2; break;
    case Fx3d::Transform:
    case Fx3d::Duplicate:
    case Fx3d::FollowPath: break;
    case Fx3d::Spin: d.speed = 0.25f; d.axis = 1; d.align = 0; break;
    }
    return d;
}

QMatrix4x4 Fx3dSettings::transform() const
{
    ModelPlacement p;
    p.position = move;
    p.rotation = turn;
    p.axisScale = size;
    return p.transform();
}

QMatrix4x4 Fx3dSettings::spin(double turns, const QVector3D &centre) const
{
    const QVector3D pivot = align == 1 ? QVector3D(move[0], move[1], move[2]) : centre;
    const QVector3D around(axis == 0 ? 1.f : 0.f, axis == 1 ? 1.f : 0.f, axis == 2 ? 1.f : 0.f);
    QMatrix4x4 m;
    m.translate(pivot);
    m.rotate(float(std::fmod(turns, 1.0) * 360.0) + angle, around);
    m.translate(-pivot);
    return m;
}

static const char *const kFx3dKeys[kFx3dCount] = {"displace", "bend", "twist", "taper", "wave", "noise", "transform", "duplicate", "follow_path", "spin"};
static const char *const kFx3dNames[kFx3dCount] = {"Displace", "Bend", "Twist", "Taper", "Wave", "Noise", "Transform 3D", "Duplicate", "Follow Path", "Spin"};
QString fx3dKey(Fx3d k) { return QString::fromLatin1(kFx3dKeys[int(k)]); }
QString fx3dName(Fx3d k) { return QString::fromLatin1(kFx3dNames[int(k)]); }
std::optional<Fx3d> fx3dFromKey(const QString &key)
{
    for (int i = 0; i < kFx3dCount; ++i)
        if (key == QLatin1String(kFx3dKeys[i])) return Fx3d(i);
    return std::nullopt;
}

// --- Built-in primitives -------------------------------------------------------------------------------------------
// One mesh each, about a unit in size (Y up), with smooth normals where the surface is round and UVs that unwrap it

namespace {
struct Builder {
    std::vector<float> data; // p, n, uv
    std::vector<quint32> indices;
    quint32 vertex(QVector3D p, QVector3D n, float u, float v)
    {
        n.normalize();
        data.insert(data.end(), {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(), u, v});
        return quint32(data.size() / 8 - 1);
    }
    void quad(quint32 a, quint32 b, quint32 c, quint32 d) { indices.insert(indices.end(), {a, b, c, a, c, d}); }
    // A flat rectangle: corner `o`, sides `du` and `dv`, its normal
    // A flat rectangle: corner `o`, sides `du` and `dv`, its normal; cut in a grid (a bend curves it smoothly)
    void face(QVector3D o, QVector3D du, QVector3D dv, int nu = 16, int nv = 16)
    {
        const QVector3D n = QVector3D::crossProduct(du, dv);
        const quint32 first = quint32(data.size() / 8);
        for (int j = 0; j <= nv; ++j)
            for (int i = 0; i <= nu; ++i) {
                const float u = float(i) / nu, v = float(j) / nv;
                vertex(o + du * u + dv * v, n, u, 1 - v);
            }
        const quint32 row = quint32(nu + 1);
        for (quint32 j = 0; j < quint32(nv); ++j)
            for (quint32 i = 0; i < quint32(nu); ++i) {
                const quint32 a = first + j * row + i;
                quad(a, a + 1, a + row + 1, a + row);
            }
    }
    // A surface of revolution around Y: its profile (radius, height, the normal's radial and vertical parts, v), turned
    struct P {
        float r, y, nr, ny, v;
    };
    void lathe(const std::vector<P> &profile, int segments)
    {
        const quint32 first = quint32(data.size() / 8);
        for (const P &p : profile)
            for (int i = 0; i <= segments; ++i) {
                const float a = float(2 * M_PI * i / segments), s = std::sin(a), c = std::cos(a);
                vertex({p.r * s, p.y, p.r * c}, {p.nr * s, p.ny, p.nr * c}, float(i) / segments, p.v);
            }
        const quint32 row = quint32(segments + 1);
        for (quint32 j = 0; j + 1 < profile.size(); ++j)
            for (quint32 i = 0; i < quint32(segments); ++i) {
                const quint32 a = first + j * row + i;
                quad(a, a + row, a + row + 1, a + 1);
            }
    }
    // A flat disc facing up (+1) or down (-1) at height y
    void disc(float y, float radius, float facing, int segments)
    {
        const quint32 centre = vertex({0, y, 0}, {0, facing, 0}, 0.5f, 0.5f);
        const quint32 first = quint32(data.size() / 8);
        for (int i = 0; i <= segments; ++i) {
            const float a = float(2 * M_PI * i / segments), s = std::sin(a), c = std::cos(a);
            vertex({radius * s, y, radius * c}, {0, facing, 0}, 0.5f + 0.5f * s, 0.5f - 0.5f * c * facing);
        }
        for (int i = 0; i < segments; ++i) {
            const quint32 a = first + quint32(i), b = a + 1;
            if (facing > 0) indices.insert(indices.end(), {centre, a, b});
            else indices.insert(indices.end(), {centre, b, a});
        }
    }
    void sphere(float radius, float y, float from, float to, float v0, float v1, int rings, int segments) // polar angles
    {
        std::vector<P> profile;
        for (int j = 0; j <= rings; ++j) {
            const float t = from + (to - from) * j / rings, s = std::sin(t), c = std::cos(t);
            profile.push_back({radius * s, y + radius * c, s, c, v0 + (v1 - v0) * j / rings});
        }
        lathe(profile, segments);
    }
};
} // namespace

bool Model3D::loadPrimitive(const QString &name, QString *err)
{
    Builder b;
    // Periodic U/V and collapsed ends of V, for stitching live displacement textures.
    if (name == QLatin1String("Torus")) m_displacementUvTopology = 1 | 2;
    else if (name == QLatin1String("Sphere") || name == QLatin1String("Capsule")) m_displacementUvTopology = 1 | 4 | 8;
    else if (name == QLatin1String("Dome")) m_displacementUvTopology = 1 | 8;
    else if (name == QLatin1String("Cylinder") || name == QLatin1String("Open Cylinder")) m_displacementUvTopology = 1;
    else m_displacementUvTopology = 0;
    // As little as the shape needs, more when asked: segments around; cuts of a flat face (a cube is 12 triangles, a plane 2)
    static const int kRound[8] = {24, 32, 48, 64, 96, 128, 192, 256}, kFlat[8] = {1, 4, 8, 16, 32, 64, 128, 256};
    const int S = kRound[std::clamp(m_detail, 1, 8) - 1], F = kFlat[std::clamp(m_detail, 1, 8) - 1];
    const float h = 0.5f;
    if (name == QLatin1String("Cube") || name == QLatin1String("Open Cube")) {
        b.face({-h, -h, h}, {1, 0, 0}, {0, 1, 0}, F, F);   // front
        b.face({h, -h, -h}, {-1, 0, 0}, {0, 1, 0}, F, F);  // back
        b.face({-h, -h, -h}, {0, 0, 1}, {0, 1, 0}, F, F);  // left
        b.face({h, -h, h}, {0, 0, -1}, {0, 1, 0}, F, F);   // right
        if (name == QLatin1String("Cube")) {         // open: no top, no bottom (a box seen through)
            b.face({-h, h, h}, {1, 0, 0}, {0, 0, -1}, F, F);   // top
            b.face({-h, -h, -h}, {1, 0, 0}, {0, 0, 1}, F, F);  // bottom
        }
    } else if (name == QLatin1String("Plane")) { // upright, facing +Z: a screen
        b.face({-h, -h, 0}, {1, 0, 0}, {0, 1, 0}, F, F);
    } else if (name == QLatin1String("Disc")) { // upright, facing +Z, in rings (a bend curves it smoothly)
        const int rings = F;
        const quint32 centre = b.vertex({0, 0, 0}, {0, 0, 1}, 0.5f, 0.5f);
        const quint32 first = quint32(b.data.size() / 8);
        for (int r = 1; r <= rings; ++r)
            for (int i = 0; i <= S; ++i) {
                const float a = float(2 * M_PI * i / S), k = h * r / rings;
                b.vertex({k * std::cos(a), k * std::sin(a), 0}, {0, 0, 1}, 0.5f + k * std::cos(a), 0.5f - k * std::sin(a));
            }
        const quint32 row = quint32(S + 1);
        for (int i = 0; i < S; ++i) b.indices.insert(b.indices.end(), {centre, first + quint32(i), first + quint32(i) + 1});
        for (quint32 r = 0; r + 1 < quint32(rings); ++r)
            for (quint32 i = 0; i < quint32(S); ++i) {
                const quint32 a = first + r * row + i;
                b.quad(a, a + row, a + row + 1, a + 1);
            }
    } else if (name == QLatin1String("Cylinder") || name == QLatin1String("Open Cylinder")) {
        b.lathe({{h, h, 1, 0, 0}, {h, -h, 1, 0, 1}}, S);
        if (name == QLatin1String("Cylinder")) { // open: a tube
            b.disc(h, h, 1, S);
            b.disc(-h, h, -1, S);
        }
    } else if (name == QLatin1String("Cone")) {
        const float nr = 1, ny = 0.5f; // its slope: a unit high, half a unit in radius
        b.lathe({{0, h, nr, ny, 0}, {h, -h, nr, ny, 1}}, S);
        b.disc(-h, h, -1, S);
    } else if (name == QLatin1String("Pyramid")) { // a square base, four triangles to its apex
        const QVector3D apex(0, h, 0), c[4] = {{-h, -h, h}, {h, -h, h}, {h, -h, -h}, {-h, -h, -h}};
        for (int k = 0; k < 4; ++k) {
            const QVector3D a = c[k], d = c[(k + 1) % 4], n = QVector3D::crossProduct(d - a, apex - a);
            b.indices.insert(b.indices.end(), {b.vertex(a, n, 0, 1), b.vertex(d, n, 1, 1), b.vertex(apex, n, 0.5f, 0)});
        }
        b.face({-h, -h, -h}, {1, 0, 0}, {0, 0, 1}, F, F);
    } else if (name == QLatin1String("Sphere")) {
        b.sphere(h, 0, 0, float(M_PI), 0, 1, S / 2, S);
    } else if (name == QLatin1String("Dome")) { // a half sphere, open below (seen from inside too: a planetarium)
        b.sphere(h, 0, 0, float(M_PI / 2), 0, 1, S / 4, S);
    } else if (name == QLatin1String("Torus")) {
        const float R = 0.35f, r = 0.15f;
        const quint32 first = 0;
        for (int j = 0; j <= S / 2; ++j)
            for (int i = 0; i <= S; ++i) {
                const float u = float(2 * M_PI * i / S), v = float(2 * M_PI * j / (S / 2));
                const QVector3D n(std::cos(v) * std::sin(u), std::sin(v), std::cos(v) * std::cos(u));
                const QVector3D p = QVector3D(R * std::sin(u), 0, R * std::cos(u)) + r * n;
                b.vertex(p, n, float(i) / S, float(j) / (S / 2));
            }
        const quint32 row = quint32(S + 1);
        for (quint32 j = 0; j < quint32(S / 2); ++j)
            for (quint32 i = 0; i < quint32(S); ++i) {
                const quint32 a = first + j * row + i;
                b.quad(a, a + 1, a + row + 1, a + row);
            }
    } else if (name == QLatin1String("Capsule")) { // two half spheres and a cylinder between them
        const float r = 0.25f;
        b.sphere(r, h - r, 0, float(M_PI / 2), 0, 0.25f, S / 4, S);
        b.lathe({{r, h - r, 1, 0, 0.25f}, {r, -h + r, 1, 0, 0.75f}}, S);
        b.sphere(r, -h + r, float(M_PI / 2), float(M_PI), 0.75f, 1, S / 4, S);
    } else {
        if (err) *err = QStringLiteral("No built-in 3D primitive called %1.").arg(name);
        return false;
    }
    Mesh mesh;
    mesh.vertices.resize(b.data.size() / 8);
    std::memcpy(mesh.vertices.data(), b.data.data(), b.data.size() * sizeof(float));
    mesh.indices = std::move(b.indices);
    m_meshes.push_back(std::move(mesh));
    return true;
}

// --- glTF / GLB (cgltf) --------------------------------------------------------------------------------------------

namespace {
using GltfDocument = std::unique_ptr<cgltf_data, decltype(&cgltf_free)>;

GltfDocument readGltf(const QString &path)
{
    cgltf_options options{};
    cgltf_data *data = nullptr;
    const QByteArray file = QFile::encodeName(path);
    const auto result = cgltf_parse_file(&options, file.constData(), &data);
    GltfDocument document(data, &cgltf_free);
    if (result != cgltf_result_success || cgltf_load_buffers(&options, data, file.constData()) != cgltf_result_success)
        document.reset();
    return document;
}

QImage readGltfImage(const cgltf_image &image, const QDir &dir)
{
    if (image.buffer_view) {
        const auto *view = image.buffer_view;
        const auto *buffer = view->buffer;
        if (!buffer || !buffer->data || view->offset > buffer->size || view->size > buffer->size - view->offset ||
            view->size > std::numeric_limits<int>::max()) return {};
        const auto *bytes = static_cast<const uchar *>(buffer->data) + view->offset;
        return QImage::fromData(bytes, int(view->size));
    }
    if (!image.uri) return {};
    QByteArray uri(image.uri);
    if (uri.startsWith("data:")) {
        const int comma = uri.indexOf(',');
        if (comma < 0 || !uri.left(comma).endsWith(";base64")) return {};
        return QImage::fromData(QByteArray::fromBase64(uri.mid(comma + 1)));
    }
    cgltf_decode_uri(uri.data());
    return QImage(dir.absoluteFilePath(QString::fromUtf8(uri.constData())));
}

QImage gltfChannel(const QImage &rgba, int channel)
{
    QImage result(rgba.size(), QImage::Format_Grayscale8);
    if (result.isNull()) return {};
    for (int y = 0; y < rgba.height(); ++y) {
        const auto *src = rgba.constScanLine(y);
        auto *dst = result.scanLine(y);
        for (int x = 0; x < rgba.width(); ++x) dst[x] = src[4 * x + channel];
    }
    return result;
}
} // namespace

std::vector<Material3D> importGltfMaterials(const QString &path, QString *error)
{
    if (error) error->clear();
    const auto document = readGltf(path);
    if (!document || document->materials_count == 0) {
        if (error) *error = document ? QStringLiteral("The glTF file has no materials.")
                                     : QStringLiteral("Not a glTF file that can be read.");
        return {};
    }
    const QFileInfo source(path);
    const QDir dir = source.absoluteDir();
    QHash<const cgltf_image *, QImage> images;
    std::vector<Material3D> materials;
    for (size_t i = 0; i < document->materials_count; ++i) {
        const auto &input = document->materials[i];
        Material3D material;
        material.name = QString::fromUtf8(input.name);
        if (material.name.isEmpty()) material.name = QStringLiteral("%1 · Material %2").arg(source.completeBaseName()).arg(i + 1);
        const auto &pbr = input.pbr_metallic_roughness;
        const auto displayColor = [](float linear) { return std::pow(std::clamp(linear, 0.f, 1.f), 1.f / 2.2f); };
        for (int c = 0; c < 3; ++c) {
            material.color[c] = displayColor(pbr.base_color_factor[c]);
            material.emissionColor[c] = displayColor(input.emissive_factor[c]);
        }
        material.metal = pbr.metallic_factor;
        material.rough = pbr.roughness_factor;
        material.emission = input.has_emissive_strength ? input.emissive_strength.emissive_strength : 1.f;
        material.normalStrength = input.normal_texture.texture ? input.normal_texture.scale : 1.f;
        material.occlusionStrength = input.occlusion_texture.texture ? input.occlusion_texture.scale : 1.f;
        if (input.has_ior) material.ior = input.ior.ior;
        if (input.has_specular) material.specular = input.specular.specular_factor;
        auto map = [&](MaterialMap slot, const cgltf_texture_view &view, int channel = -1) {
            if (!view.texture) return true;
            const auto *image = view.texture->image;
            if (!image) return false;
            auto it = images.find(image);
            if (it == images.end())
                it = images.insert(image, readGltfImage(*image, dir).convertToFormat(QImage::Format_RGBA8888));
            if (it->isNull()) return false;
            QString name = QString::fromUtf8(image->name);
            QByteArray uri(image->uri);
            if (!uri.isEmpty() && !uri.startsWith("data:")) {
                cgltf_decode_uri(uri.data());
                name = QFileInfo(QString::fromUtf8(uri.constData())).fileName();
            }
            if (name.isEmpty()) name = QStringLiteral("%1 · Image %2").arg(source.fileName()).arg(image - document->images + 1);
            return material.setEmbeddedMap(slot, channel < 0 ? *it : gltfChannel(*it, channel), name);
        };
        if (!map(BaseColorMap, pbr.base_color_texture) ||
            !map(RoughnessMap, pbr.metallic_roughness_texture, 1) ||
            !map(MetallicMap, pbr.metallic_roughness_texture, 2) ||
            !map(NormalMap, input.normal_texture) ||
            !map(OcclusionMap, input.occlusion_texture, 0) ||
            !map(EmissionMap, input.emissive_texture)) {
            if (error) *error = QStringLiteral("Cannot read or embed a texture for %1.").arg(material.name);
            return {};
        }
        materials.push_back(std::move(material));
    }
    return materials;
}

bool Model3D::loadGltf(const QString &path, QString *err)
{
    const auto document = readGltf(path);
    if (!document) {
        if (err) *err = QStringLiteral("Not a glTF file that can be read.");
        return false;
    }
    const auto *data = document.get();
    const QDir dir = QFileInfo(path).absoluteDir();
    QHash<const cgltf_image *, int> images;
    auto imageOf = [&](const cgltf_texture_view &view) -> int {
        if (!view.texture || !view.texture->image) return -1;
        const cgltf_image *img = view.texture->image;
        if (images.contains(img)) return images.value(img);
        const QImage decoded = readGltfImage(*img, dir);
        const int index = addImage(decoded);
        images.insert(img, index);
        return index;
    };
    QHash<const cgltf_material *, int> materials;
    auto materialOf = [&](const cgltf_material *m) -> int {
        if (!m) return 0;
        if (materials.contains(m)) return materials.value(m);
        Material mat;
        if (m->has_pbr_metallic_roughness) {
            const auto &pbr = m->pbr_metallic_roughness;
            mat.baseColor = {{pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]}};
            mat.metallic = pbr.metallic_factor;
            mat.roughness = pbr.roughness_factor;
            mat.texture = imageOf(pbr.base_color_texture);
            mat.roughTex = mat.metalTex = imageOf(pbr.metallic_roughness_texture); // green: roughness, blue: metal
        }
        mat.emissive = {{m->emissive_factor[0], m->emissive_factor[1], m->emissive_factor[2]}};
        if (m->has_emissive_strength)
            for (float &e : mat.emissive) e *= m->emissive_strength.emissive_strength;
        mat.emTex = imageOf(m->emissive_texture);
        mat.normalTex = imageOf(m->normal_texture);
        mat.normalScale = m->normal_texture.scale != 0 ? m->normal_texture.scale : 1;
        mat.aoTex = imageOf(m->occlusion_texture);
        mat.aoStrength = m->occlusion_texture.texture ? m->occlusion_texture.scale : 1;
        mat.alphaMode = m->alpha_mode == cgltf_alpha_mode_mask ? 1 : m->alpha_mode == cgltf_alpha_mode_blend ? 2 : 0;
        mat.alphaCutoff = m->alpha_cutoff;
        m_materials.push_back(mat);
        materials.insert(m, int(m_materials.size()) - 1);
        return int(m_materials.size()) - 1;
    };
    m_materials.emplace_back(); // 0: a primitive without a material
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node *node = &data->nodes[n];
        if (!node->mesh) continue;
        float world[16];
        cgltf_node_transform_world(node, world);
        const QMatrix4x4 m = QMatrix4x4(world).transposed(); // cgltf: column major
        const QMatrix3x3 normal = m.normalMatrix();
        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            const cgltf_primitive &prim = node->mesh->primitives[p];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute &at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) pos = at.data;
                else if (at.type == cgltf_attribute_type_normal) nrm = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv = at.data;
            }
            if (!pos) continue;
            Mesh mesh;
            mesh.material = materialOf(prim.material);
            mesh.vertices.resize(pos->count);
            for (cgltf_size i = 0; i < pos->count; ++i) {
                Vertex &v = mesh.vertices[i];
                float x[3] = {0, 0, 0}, nn[3] = {0, 0, 1}, t[2] = {0, 0};
                cgltf_accessor_read_float(pos, i, x, 3);
                if (nrm) cgltf_accessor_read_float(nrm, i, nn, 3);
                if (uv) cgltf_accessor_read_float(uv, i, t, 2);
                const QVector3D wp = m.map(QVector3D(x[0], x[1], x[2]));
                const float *nm = normal.constData();
                QVector3D wn(nm[0] * nn[0] + nm[3] * nn[1] + nm[6] * nn[2], nm[1] * nn[0] + nm[4] * nn[1] + nm[7] * nn[2],
                             nm[2] * nn[0] + nm[5] * nn[1] + nm[8] * nn[2]);
                wn.normalize();
                v = Vertex{{wp.x(), wp.y(), wp.z()}, {wn.x(), wn.y(), wn.z()}, {t[0], t[1]}};
            }
            if (prim.indices) {
                mesh.indices.resize(prim.indices->count);
                for (cgltf_size i = 0; i < prim.indices->count; ++i) mesh.indices[i] = quint32(cgltf_accessor_read_index(prim.indices, i));
            } else {
                mesh.indices.resize(pos->count);
                for (cgltf_size i = 0; i < pos->count; ++i) mesh.indices[i] = quint32(i);
            }
            if (!nrm) { // flat normals, from each triangle
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    Vertex *t3[3] = {&mesh.vertices[mesh.indices[i]], &mesh.vertices[mesh.indices[i + 1]], &mesh.vertices[mesh.indices[i + 2]]};
                    const QVector3D a(t3[0]->p[0], t3[0]->p[1], t3[0]->p[2]), b(t3[1]->p[0], t3[1]->p[1], t3[1]->p[2]),
                        c(t3[2]->p[0], t3[2]->p[1], t3[2]->p[2]);
                    const QVector3D fn = QVector3D::crossProduct(b - a, c - a).normalized();
                    for (Vertex *v : t3) v->n[0] = fn.x(), v->n[1] = fn.y(), v->n[2] = fn.z();
                }
            }
            m_meshes.push_back(std::move(mesh));
        }
    }
    return true;
}

// --- FBX, OBJ (ufbx) ------------------------------------------------------------------------------------------------

bool Model3D::loadUfbx(const QString &path, QString *err)
{
    ufbx_load_opts opts{};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;
    ufbx_error error{};
    const QByteArray file = QFile::encodeName(path);
    ufbx_scene *scene = ufbx_load_file(file.constData(), &opts, &error);
    if (!scene) {
        if (err) *err = QStringLiteral("Not a file that can be read: %1").arg(QString::fromUtf8(error.description.data, int(error.description.length)));
        return false;
    }
    const QDir dir = QFileInfo(path).absoluteDir();
    QHash<const ufbx_texture *, int> images;
    auto imageOf = [&](const ufbx_texture *t) -> int {
        if (!t) return -1;
        if (images.contains(t)) return images.value(t);
        QImage decoded;
        if (t->content.size) decoded = QImage::fromData(static_cast<const uchar *>(t->content.data), int(t->content.size));
        if (decoded.isNull() && t->filename.length) {
            const QString name = QString::fromUtf8(t->filename.data, int(t->filename.length));
            decoded = QImage(QFileInfo(name).isAbsolute() && QFileInfo::exists(name) ? name : dir.absoluteFilePath(name));
            if (decoded.isNull()) decoded = QImage(dir.absoluteFilePath(QFileInfo(name).fileName()));
        }
        const int index = addImage(decoded);
        images.insert(t, index);
        return index;
    };
    QHash<const ufbx_material *, int> materials;
    auto materialOf = [&](const ufbx_material *m) -> int {
        if (!m) return 0;
        if (materials.contains(m)) return materials.value(m);
        Material mat;
        const ufbx_material_map &base = m->pbr.base_color.has_value ? m->pbr.base_color : m->fbx.diffuse_color;
        if (base.has_value) mat.baseColor = {{float(base.value_vec4.x), float(base.value_vec4.y), float(base.value_vec4.z), 1}};
        if (m->pbr.base_factor.has_value)
            for (int c = 0; c < 3; ++c) mat.baseColor[size_t(c)] *= float(m->pbr.base_factor.value_real);
        mat.texture = imageOf(m->pbr.base_color.texture ? m->pbr.base_color.texture : m->fbx.diffuse_color.texture);
        if (m->pbr.metalness.has_value) mat.metallic = float(m->pbr.metalness.value_real);
        if (m->pbr.roughness.has_value) mat.roughness = float(m->pbr.roughness.value_real);
        // Its maps: each a grey picture (its red channel)
        if ((mat.roughTex = imageOf(m->pbr.roughness.texture)) >= 0) {
            mat.roughChannel = 0;
            if (mat.roughness <= 0) mat.roughness = 1;
        }
        if ((mat.metalTex = imageOf(m->pbr.metalness.texture)) >= 0) {
            mat.metalChannel = 0;
            if (mat.metallic <= 0) mat.metallic = 1;
        }
        mat.normalTex = imageOf(m->pbr.normal_map.texture ? m->pbr.normal_map.texture : m->fbx.normal_map.texture);
        mat.aoTex = imageOf(m->pbr.ambient_occlusion.texture);
        mat.emTex = imageOf(m->pbr.emission_color.texture);
        if (mat.emTex >= 0 && mat.emissive == std::array<float, 3>{{0, 0, 0}}) mat.emissive = {{1, 1, 1}};
        if (m->pbr.opacity.has_value && m->pbr.opacity.value_real < 0.999) {
            mat.baseColor[3] = float(m->pbr.opacity.value_real);
            mat.alphaMode = 2;
        }
        if (m->pbr.emission_color.has_value) {
            const float k = m->pbr.emission_factor.has_value ? float(m->pbr.emission_factor.value_real) : 1.0f;
            mat.emissive = {{float(m->pbr.emission_color.value_vec4.x) * k, float(m->pbr.emission_color.value_vec4.y) * k,
                             float(m->pbr.emission_color.value_vec4.z) * k}};
        }
        m_materials.push_back(mat);
        materials.insert(m, int(m_materials.size()) - 1);
        return int(m_materials.size()) - 1;
    };
    m_materials.emplace_back(); // 0: a face without a material
    for (size_t n = 0; n < scene->nodes.count; ++n) {
        const ufbx_node *node = scene->nodes.data[n];
        const ufbx_mesh *mesh = node->mesh;
        if (!mesh) continue;
        const ufbx_matrix toWorld = node->geometry_to_world;
        const ufbx_matrix normalToWorld = ufbx_matrix_for_normals(&toWorld);
        const QString key = QString::number(n);
        if (QFileInfo(path).suffix().compare(QStringLiteral("fbx"), Qt::CaseInsensitive) == 0)
            m_objects.emplace_back(key, QString::fromUtf8(node->name.data, qsizetype(node->name.length)));
        // One mesh of ours per material of it
        std::vector<Mesh> parts(std::max<size_t>(1, mesh->materials.count));
        for (size_t k = 0; k < parts.size(); ++k)
            parts[k].material = materialOf(k < mesh->materials.count ? mesh->materials.data[k] : nullptr);
        std::vector<uint32_t> tri(mesh->max_face_triangles * 3);
        for (size_t f = 0; f < mesh->faces.count; ++f) {
            const ufbx_face face = mesh->faces.data[f];
            const size_t part = f < mesh->face_material.count ? std::min<size_t>(mesh->face_material.data[f], parts.size() - 1) : 0;
            Mesh &out = parts[part];
            const size_t count = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
            for (size_t i = 0; i < count * 3; ++i) {
                const uint32_t ix = tri[i];
                const ufbx_vec3 p = ufbx_transform_position(&toWorld, ufbx_get_vertex_vec3(&mesh->vertex_position, ix));
                ufbx_vec3 nn{};
                if (mesh->vertex_normal.exists) nn = ufbx_get_vertex_vec3(&mesh->vertex_normal, ix);
                else nn.z = 1;
                nn = ufbx_vec3_normalize(ufbx_transform_direction(&normalToWorld, nn));
                const ufbx_vec2 uv = mesh->vertex_uv.exists ? ufbx_get_vertex_vec2(&mesh->vertex_uv, ix) : ufbx_vec2{};
                out.indices.push_back(quint32(out.vertices.size()));
                out.vertices.push_back(Vertex{{float(p.x), float(p.y), float(p.z)}, {float(nn.x), float(nn.y), float(nn.z)},
                                              {float(uv.x), 1.0f - float(uv.y)}}); // its V from the bottom: ours from the top
            }
        }
        for (Mesh &part : parts)
            if (!part.indices.empty()) { part.object = key; m_meshes.push_back(std::move(part)); }
    }
    ufbx_free_scene(scene);
    return true;
}

void MaterialTextureCache::applyParameters(const Material3D &material, ModelPlacement &placement)
{
    placement.sharedMaterial = true;
    placement.color = material.color;
    placement.metal = material.metal;
    placement.rough = material.rough;
    placement.uvTiling = material.uvTiling;
    placement.normalStrength = material.normalStrength;
    placement.occlusionStrength = material.occlusionStrength;
    placement.ior = material.ior;
    placement.specular = material.specular;
    for (int channel = 0; channel < 3; ++channel)
        placement.emission[channel] = std::pow(material.emissionColor[channel], 2.2f) * material.emission;
}

// Each point's normal, the average of the faces around it: points at one place with different normals (the edges of a
// cube, of a file's hard edges) are one point here
static_assert(sizeof(float) * 8 == 32);
static void averageNormals(std::vector<std::array<float, 8>> &points, const std::vector<quint32> &indices, float size)
{
    const float step = std::max(size, 1e-6f) * 1e-4f;
    const auto keyOf = [&](const std::array<float, 8> &p) {
        return std::array<qint64, 3>{qint64(std::llround(p[0] / step)), qint64(std::llround(p[1] / step)), qint64(std::llround(p[2] / step))};
    };
    std::map<std::array<qint64, 3>, QVector3D> sums;
    for (size_t k = 0; k + 2 < indices.size(); k += 3) {
        const auto &a = points[indices[k]], &b = points[indices[k + 1]], &c = points[indices[k + 2]];
        QVector3D face = QVector3D::crossProduct(QVector3D(b[0] - a[0], b[1] - a[1], b[2] - a[2]), QVector3D(c[0] - a[0], c[1] - a[1], c[2] - a[2]));
        // On the side its normals say (a file may wind its faces either way)
        if (QVector3D::dotProduct(face, QVector3D(a[3] + b[3] + c[3], a[4] + b[4] + c[4], a[5] + b[5] + c[5])) < 0) face = -face;
        for (quint32 i : {indices[k], indices[k + 1], indices[k + 2]}) sums[keyOf(points[i])] += face;
    }
    for (auto &p : points) {
        const QVector3D n = sums[keyOf(p)].normalized();
        if (n.isNull()) continue;
        p[3] = n.x();
        p[4] = n.y();
        p[5] = n.z();
    }
}

QMatrix4x4 ModelPlacement::transform() const
{
    QMatrix4x4 m;
    m.translate(position[0], position[1], position[2]);
    m.rotate(rotation[2], 0, 0, 1);
    m.rotate(rotation[1], 0, 1, 0);
    m.rotate(rotation[0], 1, 0, 0);
    m.scale(scale * axisScale[0], scale * axisScale[1], scale * axisScale[2]);
    return m;
}

QMatrix4x4 Model3D::matrix(const ModelPlacement &p) const
{
    QMatrix4x4 m;
    m.translate(p.position[0], p.position[1], p.position[2]);
    m.rotate(p.rotation[2], 0, 0, 1);
    m.rotate(p.rotation[1], 0, 1, 0);
    m.rotate(p.rotation[0], 1, 0, 0);
    m.scale(p.scale * p.axisScale[0], p.scale * p.axisScale[1], p.scale * p.axisScale[2]);
    if (p.fit) {
        m.scale(1.0f / m_radius);
        m.translate(-m_center);
    }
    return m;
}

// The index pairs of a mesh's edges (for the QRhi renderer)
std::vector<quint32> Model3D::edgeIndices(const Mesh &m) const
{
    std::vector<quint32> edges;
    const size_t triangles = m.indices.size() / 3;
    if (triangles <= 200000) {
        const float flat = triangles > 20000 ? std::cos(qDegreesToRadians(30.0f)) : 0.9999f;
        // The same point in faces that have their own normals or UVs is one point
        std::map<std::array<qint64, 3>, quint32> points;
        const float grid = 1e5f / std::max(m_radius, 1e-6f);
        std::vector<quint32> point(m.vertices.size());
        for (size_t k = 0; k < m.vertices.size(); ++k) {
            const float *q = m.vertices[k].p;
            point[k] = points.try_emplace({{qint64(std::llround(q[0] * grid)), qint64(std::llround(q[1] * grid)), qint64(std::llround(q[2] * grid))}}, quint32(k)).first->second;
        }
        struct Edge {
            QVector3D normal;
            bool shown = true;
        };
        std::map<std::pair<quint32, quint32>, Edge> seen;
        const auto at = [&](quint32 k) { return QVector3D(m.vertices[k].p[0], m.vertices[k].p[1], m.vertices[k].p[2]); };
        for (size_t t = 0; t < triangles; ++t) {
            const quint32 a = point[m.indices[3 * t]], b = point[m.indices[3 * t + 1]], c = point[m.indices[3 * t + 2]];
            const QVector3D normal = QVector3D::crossProduct(at(b) - at(a), at(c) - at(a)).normalized();
            for (const auto &[from, to] : {std::pair(a, b), std::pair(b, c), std::pair(c, a)}) {
                if (from == to) continue;
                const auto [edge, isNew] = seen.try_emplace(std::minmax(from, to), Edge{normal, true});
                if (!isNew) edge->second.shown = std::abs(QVector3D::dotProduct(edge->second.normal, normal)) < flat;
            }
        }
        edges.reserve(seen.size() * 2);
        for (const auto &[ends, edge] : seen)
            if (edge.shown) {
                edges.push_back(ends.first);
                edges.push_back(ends.second);
            }
    }
    return edges;
}

bool Model3D::hasBlended() const
{
    for (const Mesh &m : m_meshes)
        if (!m_materials.empty() && m_materials[size_t(std::clamp(m.material, 0, int(m_materials.size()) - 1))].alphaMode == 2) return true;
    return false;
}

void Model3D::setSmooth(bool on)
{
    if (on == m_smooth) return;
    m_smooth = on;
    releaseRhi();
}

void Model3D::setDetail(int detail)
{
    detail = std::clamp(detail, 1, 8);
    if (!m_primitive || detail == m_detail) return;
    m_detail = detail;
    ++m_revision;
    releaseRhi(); // made again, uploaded when next drawn
    m_meshes.clear();
    QString err;
    loadPrimitive(m_path.section('/', -1), &err);
    computeBounds();
}

QVector3D Camera3D::aim(float yawDeg, float pitchDeg)
{
    const float y = qDegreesToRadians(yawDeg), p = qDegreesToRadians(pitchDeg);
    return QVector3D(std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y));
}

void Camera3D::anglesOf(const QVector3D &direction, float *yawDeg, float *pitchDeg)
{
    const QVector3D d = direction.normalized();
    *yawDeg = qRadiansToDegrees(std::atan2(d.x(), d.z()));
    *pitchDeg = qRadiansToDegrees(std::asin(std::clamp(d.y(), -1.0f, 1.0f)));
}

Camera3D Camera3D::of(const SceneSettings &s, float aspect)
{
    Camera3D c;
    const QVector3D target(s.target[0], s.target[1], s.target[2]);
    const float distance = std::max(0.01f, s.distance);
    c.eye = target + aim(s.yaw, s.pitch) * distance;
    c.view.lookAt(c.eye, target, QVector3D(0, 1, 0));
    c.nearPlane = distance * 0.01f;
    c.farPlane = distance * 50.0f + 100.0f;
    c.proj.perspective(std::clamp(s.fov, 1.0f, 170.0f), aspect, c.nearPlane, c.farPlane);
    return c;
}

bool Scene3D::lite(const QString &renderer)
{
    // The renderers that run on the processor and are too slow for the costly passes: Apple's, Direct3D's WARP (the
    // Windows runners: a frame of a lit scene takes tens of seconds there). Mesa's llvmpipe and lavapipe keep up.
    for (const char *name : {"Apple Software Renderer", "Microsoft Basic Render", "WARP"})
        if (renderer.contains(QLatin1String(name), Qt::CaseInsensitive)) return true;
    return qEnvironmentVariableIsSet("NYXMAP_3D_LITE");
}

// ---------------------------------------------------------------------------------------------------------------------
// QRhi
// ---------------------------------------------------------------------------------------------------------------------
struct Model3D::RhiData {
    QRhi *rhi = nullptr;
    struct MeshBuffers {
        std::unique_ptr<QRhiBuffer> vertices, indices, edges;
        int edgeCount = 0;
        bool edgesMade = false;
    };
    std::vector<MeshBuffers> meshes;
    std::vector<std::unique_ptr<QRhiTexture>> textures;
};

void Model3D::releaseRhi() { m_rhi.reset(); }

std::vector<Model3D::RhiDraw> Model3D::rhiDraws(QRhi *rhi, QRhiResourceUpdateBatch *u, const ModelPlacement *look, bool picture, bool edges)
{
    std::vector<RhiDraw> out;
    if (!rhi) return out;
    if (m_rhi && m_rhi->rhi != rhi) m_rhi.reset();
    if (!m_rhi) {
        m_rhi = std::make_unique<RhiData>();
        m_rhi->rhi = rhi;
        for (Mesh &m : m_meshes) {
            RhiData::MeshBuffers b;
            std::vector<std::array<float, 8>> smooth;
            const void *vertices = m.vertices.data();
            if (m_smooth) { // a copy: its own normals come back when it is asked
                smooth.resize(m.vertices.size());
                std::memcpy(smooth.data(), m.vertices.data(), smooth.size() * sizeof(smooth[0]));
                averageNormals(smooth, m.indices, m_radius);
                vertices = smooth.data();
            }
            const quint32 vbytes = quint32(m.vertices.size() * sizeof(Vertex)), ibytes = quint32(m.indices.size() * sizeof(quint32));
            if (vbytes && ibytes) {
                b.vertices.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vbytes));
                b.indices.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, ibytes));
                if (b.vertices->create() && b.indices->create()) {
                    u->uploadStaticBuffer(b.vertices.get(), vertices);
                    u->uploadStaticBuffer(b.indices.get(), m.indices.data());
                } else {
                    b.vertices.reset();
                    b.indices.reset();
                }
            }
            m_rhi->meshes.push_back(std::move(b));
        }
        for (const QImage &image : m_images) {
            const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
            std::unique_ptr<QRhiTexture> t(rhi->newTexture(QRhiTexture::RGBA8, rgba.size(), 1, QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
            if (t->create()) {
                u->uploadTexture(t.get(), rgba);
                u->generateMips(t.get());
            } else {
                t.reset();
            }
            m_rhi->textures.push_back(std::move(t));
        }
    }
    auto image = [&](int index) -> QRhiTexture * { return index >= 0 && size_t(index) < m_rhi->textures.size() ? m_rhi->textures[size_t(index)].get() : nullptr; };
    for (size_t k = 0; k < m_meshes.size() && k < m_rhi->meshes.size(); ++k) {
        const Mesh &m = m_meshes[k];
        RhiData::MeshBuffers &b = m_rhi->meshes[k];
        if (!b.vertices) continue;
        Material mat = m_materials.empty() ? Material() : m_materials[size_t(std::clamp(m.material, 0, int(m_materials.size()) - 1))];
        if (look && look->sharedMaterial) mat = Material(); // the project's material replaces the file's: its maps are below
        if (look && (m_primitive || look->sharedMaterial)) { // a primitive: the material its parameters give it
            mat.baseColor = {{std::pow(look->color[0], 2.2f), std::pow(look->color[1], 2.2f), std::pow(look->color[2], 2.2f), 1}};
            mat.emissive = look->emission;
        }
        if (picture) { // a layer's picture instead of its colours (a white material under it)
            mat.baseColor = {{1, 1, 1, 1}};
            mat.emissive = {{0, 0, 0}};
            mat.alphaMode = 0;
        }
        RhiDraw d;
        d.vertices = b.vertices.get();
        d.indices = b.indices.get();
        d.indexCount = int(m.indices.size());
        d.baseColor = mat.baseColor;
        d.emissive = mat.emissive;
        d.alphaCutoff = mat.alphaCutoff;
        d.alphaMode = picture ? 2 : mat.alphaMode; // a picture keeps its alpha
        d.blended = !picture && mat.alphaMode == 2;
        d.baseTexture = look && look->sharedMaterial ? look->materialRhi[BaseColorMap] : image(mat.texture);
        if (look && (m_primitive || look->sharedMaterial)) { // a primitive: the material its parameters give it
            mat.metallic = look->metal;
            mat.roughness = look->rough;
            mat.normalScale = look->normalStrength;
            mat.aoStrength = look->occlusionStrength;
            mat.roughChannel = mat.metalChannel = 0;
        }
        d.metallic = mat.metallic;
        d.roughness = mat.roughness;
        const float ior = look ? look->ior : 1.5f, reflectance = (ior - 1) / (ior + 1);
        d.dielectric = reflectance * reflectance * (look ? look->specular : 1.f);
        d.normalScale = mat.normalScale;
        d.aoStrength = mat.aoStrength;
        const auto channel = [](int c) { return std::array<float, 4>{{float(c == 0), float(c == 1), float(c == 2), float(c == 3)}}; };
        d.roughChannel = channel(mat.roughChannel);
        d.metalChannel = channel(mat.metalChannel);
        const int indices[5] = {mat.roughTex, mat.metalTex, mat.normalTex, mat.aoTex, mat.emTex};
        for (int k = 0; k < 5; ++k) d.maps[size_t(k)] = look && look->sharedMaterial ? look->materialRhi[size_t(RoughnessMap + k)] : image(indices[k]);
        if (edges) {
            if (!b.edgesMade) {
                b.edgesMade = true;
                const std::vector<quint32> e = edgeIndices(m);
                b.edgeCount = int(e.size());
                if (!e.empty()) {
                    b.edges.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, quint32(e.size() * sizeof(quint32))));
                    if (b.edges->create()) u->uploadStaticBuffer(b.edges.get(), e.data());
                    else b.edges.reset();
                }
            }
            d.edges = b.edges.get();
            d.edgeCount = b.edgeCount;
        }
        out.push_back(d);
    }
    return out;
}
