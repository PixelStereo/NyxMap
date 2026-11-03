#include "Model3D.h"

#include <QtGlobal>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "ufbx.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMatrix4x4>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

Model3D::Model3D() = default;
Model3D::~Model3D() = default; // its GL objects go with releaseGl, on the render thread

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

bool Model3D::load(const QString &path, QString *err)
{
    m_path = path;
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
    computeBounds();
    return true;
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
    m_radius = std::max(1e-6f, (hi - lo).length() / 2);
    m_width = std::max(1e-6f, hi.x() - lo.x());
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
    const int S = 64 * m_detail, F = 16 * m_detail; // segments around; cuts of a flat face
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
        b.face({-h, -h, 0}, {1, 0, 0}, {0, 1, 0}, S, S);
    } else if (name == QLatin1String("Disc")) { // upright, facing +Z, in rings (a bend curves it smoothly)
        const int rings = 16 * m_detail;
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

bool Model3D::loadGltf(const QString &path, QString *err)
{
    cgltf_options options{};
    cgltf_data *data = nullptr;
    const QByteArray file = QFile::encodeName(path);
    if (cgltf_parse_file(&options, file.constData(), &data) != cgltf_result_success ||
        cgltf_load_buffers(&options, data, file.constData()) != cgltf_result_success) {
        if (data) cgltf_free(data);
        if (err) *err = QStringLiteral("Not a glTF file that can be read.");
        return false;
    }
    const QDir dir = QFileInfo(path).absoluteDir();
    // Its images, decoded once: embedded (a buffer, a data URI) or beside the file
    QHash<const cgltf_image *, int> images;
    auto imageOf = [&](const cgltf_texture_view &view) -> int {
        if (!view.texture || !view.texture->image) return -1;
        const cgltf_image *img = view.texture->image;
        if (images.contains(img)) return images.value(img);
        QImage decoded;
        if (img->buffer_view && img->buffer_view->buffer->data) {
            const auto *bytes = static_cast<const uchar *>(img->buffer_view->buffer->data) + img->buffer_view->offset;
            decoded = QImage::fromData(bytes, int(img->buffer_view->size));
        } else if (img->uri) {
            const QString uri = QString::fromUtf8(img->uri);
            if (uri.startsWith(QLatin1String("data:"))) {
                decoded = QImage::fromData(QByteArray::fromBase64(uri.section(',', 1).toLatin1()));
            } else {
                QByteArray name = QByteArray(img->uri);
                cgltf_decode_uri(name.data());
                decoded = QImage(dir.absoluteFilePath(QString::fromUtf8(name.constData())));
            }
        }
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
    cgltf_free(data);
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
                ufbx_vec3 nn = mesh->vertex_normal.exists ? ufbx_get_vertex_vec3(&mesh->vertex_normal, ix) : ufbx_vec3{0, 0, 1};
                nn = ufbx_vec3_normalize(ufbx_transform_direction(&normalToWorld, nn));
                const ufbx_vec2 uv = mesh->vertex_uv.exists ? ufbx_get_vertex_vec2(&mesh->vertex_uv, ix) : ufbx_vec2{0, 0};
                out.indices.push_back(quint32(out.vertices.size()));
                out.vertices.push_back(Vertex{{float(p.x), float(p.y), float(p.z)}, {float(nn.x), float(nn.y), float(nn.z)},
                                              {float(uv.x), 1.0f - float(uv.y)}}); // its V from the bottom: ours from the top
            }
        }
        for (Mesh &part : parts)
            if (!part.indices.empty()) m_meshes.push_back(std::move(part));
    }
    ufbx_free_scene(scene);
    return true;
}

// --- Drawing ---------------------------------------------------------------------------------------------------------

static const char *kVertex = R"(#version 330 core
layout(location=0) in vec3 a_pos; layout(location=1) in vec3 a_n; layout(location=2) in vec2 a_uv;
uniform mat4 u_model, u_view, u_proj; uniform mat3 u_normal;
uniform float u_bend, u_bendCx, u_bendW; // radians across its width, around an upright axis in front of it
uniform int u_hasDisp; uniform sampler2D u_disp; uniform float u_dispAmount, u_dispMid; // pushed along its normals
out vec3 v_pos; out vec3 v_n; out vec2 v_uv;
void main() {
    vec3 p = a_pos, n = a_n;
    if (u_hasDisp != 0) {
        vec4 d = textureLod(u_disp, vec2(a_uv.x, 1.0 - a_uv.y), 0.0); // a layer's picture: its first row at the bottom
        float l = dot(d.rgb, vec3(0.2126, 0.7152, 0.0722)) * d.a; // its luminance
        p += normalize(n) * (l - u_dispMid) * u_dispAmount;
    }
    if (abs(u_bend) > 1e-4) {
        float R = u_bendW / u_bend, t = (p.x - u_bendCx) / R, s = sin(t), c = cos(t);
        p = vec3(u_bendCx + (R - p.z) * s, p.y, R - (R - p.z) * c);
        n = vec3(n.x * c - n.z * s, n.y, n.x * s + n.z * c);
    }
    vec4 w = u_model * vec4(p, 1.0);
    v_pos = w.xyz; v_n = u_normal * n; v_uv = a_uv;
    gl_Position = u_proj * u_view * w;
}
)";

// Physically based (GGX, Smith, Schlick), in linear light: its base colour (times its texture's), metal and roughness
// (factors, times their textures'), normals (its normal map, from the screen-space frame: no tangents needed),
// occlusion, emission; under the ambient light, up to eight lights and the environment (a layer's picture,
// equirectangular: diffuse from its most blurred level, reflections from the level its roughness asks); exposure, ACES
// tone mapping, sRGB out
static const char *kFragment = R"(#version 330 core
in vec3 v_pos; in vec3 v_n; in vec2 v_uv; out vec4 o;
uniform vec4 u_base; uniform float u_metal, u_rough; uniform vec3 u_emissive;
uniform int u_hasTex; uniform sampler2D u_tex; uniform int u_unlit; uniform int u_hasDisp; uniform int u_layerTex;
uniform sampler2D u_roughTex, u_metalTex, u_normalTex, u_aoTexMap, u_emTex;
uniform int u_hasRough, u_hasMetal, u_hasNormal, u_hasAo, u_hasEm;
uniform vec4 u_roughCh, u_metalCh; uniform float u_normalScale, u_aoStrength;
uniform int u_alphaMode; uniform float u_alphaCutoff; // 0 opaque, 1 mask, 2 blend
uniform vec3 u_eye, u_ambient; uniform float u_exposure;
uniform int u_count; uniform int u_type[8]; uniform vec3 u_color[8], u_lpos[8], u_ldir[8]; uniform float u_cos[8];
uniform int u_hasEnv; uniform sampler2D u_env; uniform float u_envIntensity, u_envRot, u_envMaxLod; uniform int u_envLinear;
uniform float u_cosIn[8]; uniform float u_studio;
uniform int u_taps, u_steps; uniform int u_pointShadow[8]; uniform float u_pointFar[2], u_pointSoft[2]; uniform samplerCubeShadow u_pointMap0, u_pointMap1;
uniform sampler2D u_aoTex, u_seen, u_last; uniform vec2 u_screen; uniform float u_aoAmount, u_reflections; uniform int u_hasLast;
uniform mat4 u_viewProj; uniform vec2 u_depthRange; uniform float u_lastExposure;
uniform int u_shadow[8]; uniform mat4 u_shadowMat[4]; uniform float u_shadowRadius[4], u_shadowBias[4]; uniform sampler2DArrayShadow u_shadows;
const float PI = 3.14159265;
float noise() { return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))); }
// How much of a light gets here: its view of the models, sampled around this point, turned at each pixel (the noise of a
// soft edge is finer than its bands)
float lit(int i, vec3 N) {
    int s = u_shadow[i];
    int p = u_pointShadow[i];
    if (p >= 0) { // a point light: the distances all around it
        vec3 d = v_pos + N * 0.012 * sqrt(max(length(v_pos - u_lpos[i]), 0.1)) - u_lpos[i];
        float ref = length(d) / u_pointFar[p] - 0.0006;
        vec3 x = normalize(cross(d, abs(d.y) > 0.9 * length(d) ? vec3(1, 0, 0) : vec3(0, 1, 0))), y = normalize(cross(d, x));
        float angle = noise() * 6.2831853, sum = 0.0, spread = u_pointSoft[p] * 0.0018 * length(d);
        for (int k = 0; k < u_taps; ++k) {
            float rr = sqrt((float(k) + 0.5) / float(u_taps)), aa = angle + float(k) * 2.39996323;
            vec3 q = d + (x * cos(aa) + y * sin(aa)) * rr * spread;
            sum += p == 0 ? texture(u_pointMap0, vec4(q, ref)) : texture(u_pointMap1, vec4(q, ref));
        }
        return sum / float(u_taps);
    }
    if (s < 0) return 1.0;
    vec4 q = u_shadowMat[s] * vec4(v_pos + N * u_shadowBias[s], 1.0);
    vec3 t = q.xyz / q.w * 0.5 + 0.5;
    if (q.w <= 0.0 || t.x <= 0.0 || t.x >= 1.0 || t.y <= 0.0 || t.y >= 1.0 || t.z >= 1.0) return 1.0;
    float angle = noise() * 6.2831853, sum = 0.0;
    float c = cos(angle), si = sin(angle);
    for (int k = 0; k < u_taps; ++k) {
        float rr = sqrt((float(k) + 0.5) / float(u_taps)), aa = float(k) * 2.39996323;
        vec2 d = vec2(cos(aa), sin(aa)) * rr;
        sum += texture(u_shadows, vec4(t.xy + vec2(c * d.x - si * d.y, si * d.x + c * d.y) * u_shadowRadius[s], float(s), t.z));
    }
    return sum / float(u_taps);
}
float panel(vec3 d, vec3 at, float size, float blur) { return smoothstep(size - blur, size + blur * 0.4, dot(d, normalize(at))); }
// A photo studio: a large panel above, a smaller one behind on each side, a dark room; blurred as the surface is rough
vec3 studio(vec3 d, float r) {
    float b = 0.05 + r * r * 0.9;
    vec3 c = mix(vec3(0.012, 0.012, 0.014), vec3(0.075, 0.08, 0.09), smoothstep(-0.25, 0.9, d.y));
    c += vec3(1.0, 0.97, 0.92) * 2.4 * panel(d, vec3(0.25, 0.9, 0.35), 0.86, b);
    c += vec3(0.8, 0.88, 1.0) * 1.2 * panel(d, vec3(-0.85, 0.25, -0.45), 0.94, b);
    c += vec3(1.0, 0.9, 0.82) * 0.8 * panel(d, vec3(0.9, 0.05, 0.3), 0.97, b);
    return c * u_studio;
}
// The reverse of what makes a pixel: tone mapping and display encoding (a reflection reads the last frame's)
vec3 unprocessed(vec3 c) {
    c = pow(clamp(c, 0.0, 0.985), vec3(2.2));
    vec3 a = 2.51 - 2.43 * c, b = 0.03 - 0.59 * c, d = -0.14 * c;
    return (-b + sqrt(max(b * b - 4.0 * a * d, vec3(0.0)))) / (2.0 * a) / max(u_lastExposure, 1e-3);
}
float eyeDepth(float z) { return u_depthRange.x * u_depthRange.y / (u_depthRange.y - z * (u_depthRange.y - u_depthRange.x)); }
// What the mirror direction meets on the screen; its weight in w (0: nothing)
vec4 reflected(vec3 R, float rough) {
    if (u_hasLast == 0 || u_reflections <= 0.0 || rough > 0.65) return vec4(0.0);
    // Coming back towards the camera, it would look for what the screen does not show: the far side of things
    float away = smoothstep(0.25, -0.1, dot(R, normalize(u_eye - v_pos)));
    if (away <= 0.0) return vec4(0.0);
    float span = length(u_eye - v_pos) * 0.05;
    vec3 p = v_pos + R * span * 0.3;
    float jitter = 0.9 + 0.2 * noise();
    float grow = pow(1.07, 40.0 / float(u_steps)); // reaching as far, in fewer steps or more
    span *= 40.0 / float(u_steps);
    for (int k = 0; k < u_steps; ++k) {
        p += R * span * jitter;
        span *= grow;
        vec4 q = u_viewProj * vec4(p, 1.0);
        if (q.w <= 0.0) return vec4(0.0);
        vec3 s = q.xyz / q.w * 0.5 + 0.5;
        if (s.x <= 0.0 || s.x >= 1.0 || s.y <= 0.0 || s.y >= 1.0 || s.z >= 1.0) return vec4(0.0);
        float seen = texture(u_seen, s.xy).r;
        if (seen >= 1.0) continue; // the sky: nothing to meet
        float behind = eyeDepth(s.z) - eyeDepth(seen);
        if (behind > 0.0) {
            if (behind > span * 1.2) continue; // behind a surface too far in front: passing under it
            for (int b = 0; b < 5; ++b) { // where it crossed
                span *= 0.5;
                p -= R * span * 0.5 * (behind > 0.0 ? 1.0 : -1.0);
                q = u_viewProj * vec4(p, 1.0);
                s = q.xyz / q.w * 0.5 + 0.5;
                behind = eyeDepth(s.z) - eyeDepth(texture(u_seen, s.xy).r);
            }
            vec4 c = texture(u_last, s.xy);
            if (c.a <= 0.0) return vec4(0.0);
            vec2 edge = smoothstep(vec2(0.0), vec2(0.08), s.xy) * smoothstep(vec2(0.0), vec2(0.08), 1.0 - s.xy);
            float fade = away * edge.x * edge.y * (1.0 - float(k) / float(u_steps)) * smoothstep(0.65, 0.15, rough);
            return vec4(unprocessed(c.rgb / c.a), fade * u_reflections);
        }
    }
    return vec4(0.0);
}
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
vec3 env(vec3 d, float lod) {
    float c = cos(u_envRot), s = sin(u_envRot);
    d = vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
    vec2 uv = vec2(0.5 + atan(d.x, -d.z) / (2.0 * PI), acos(clamp(d.y, -1.0, 1.0)) / PI);
    vec3 e = textureLod(u_env, vec2(uv.x, 1.0 - uv.y), lod).rgb; // bottom up, as a layer's picture
    return (u_envLinear != 0 ? e : pow(e, vec3(2.2))) * u_envIntensity; // an HDR file: linear already
}
vec2 envBrdf(float NdV, float r) { // its response to an environment (Karis' approximation of the BRDF table)
    vec4 k = r * vec4(-1.0, -0.0275, -0.572, 0.022) + vec4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(k.x * k.x, exp2(-9.28 * NdV)) * k.x + k.y;
    return vec2(-1.04, 1.04) * a004 + k.zw;
}
void main() {
    vec4 base = u_base;
    if (u_hasTex != 0) {
        vec4 t = texture(u_tex, u_layerTex != 0 ? vec2(v_uv.x, 1.0 - v_uv.y) : v_uv); // a layer's picture: bottom up
        if (u_unlit != 0) { o = vec4(t.rgb * u_base.rgb * t.a * u_base.a, t.a * u_base.a); return; } // as it is: a screen
        base *= vec4(pow(t.rgb, vec3(2.2)), t.a);
    } else if (u_unlit != 0) { o = vec4(u_base.rgb * u_base.a, u_base.a); return; }
    if (u_alphaMode == 1 && base.a < u_alphaCutoff) discard;
    if (u_alphaMode == 0) base.a = 1.0;
    vec3 N = u_hasDisp != 0 ? normalize(cross(dFdx(v_pos), dFdy(v_pos))) : normalize(v_n); // displaced: its own surface
    vec3 V = normalize(u_eye - v_pos);
    if (dot(N, V) < 0.0) N = -N; // both faces lit
    if (u_hasNormal != 0) { // its normal map, in the frame its UVs make on screen
        vec3 dp1 = dFdx(v_pos), dp2 = dFdy(v_pos);
        vec2 du1 = dFdx(v_uv), du2 = dFdy(v_uv);
        vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
        vec3 T = dp2perp * du1.x + dp1perp * du2.x, B = dp2perp * du1.y + dp1perp * du2.y;
        float invmax = inversesqrt(max(dot(T, T), dot(B, B)) + 1e-12);
        vec3 m = texture(u_normalTex, v_uv).xyz * 2.0 - 1.0;
        m.xy *= u_normalScale;
        N = normalize(mat3(T * invmax, -B * invmax, N) * m);
    }
    float rough = u_rough * (u_hasRough != 0 ? dot(texture(u_roughTex, v_uv), u_roughCh) : 1.0);
    float metal = u_metal * (u_hasMetal != 0 ? dot(texture(u_metalTex, v_uv), u_metalCh) : 1.0);
    float ao = u_hasAo != 0 ? mix(1.0, texture(u_aoTexMap, v_uv).r, u_aoStrength) : 1.0;
    float contact = u_aoAmount > 0.0 ? texture(u_aoTex, gl_FragCoord.xy / u_screen).r : 1.0;
    ao *= contact;
    float r = clamp(rough, 0.04, 1.0), a = r * r;
    vec3 F0 = mix(vec3(0.04), base.rgb, metal);
    float NdV = max(dot(N, V), 1e-4);
    vec3 c = u_ambient * base.rgb * (1.0 - 0.7 * metal) * ao;
    if (u_hasEnv != 0) { // the environment: diffuse from its most blurred level, reflections as rough as the surface
        vec2 ab = envBrdf(NdV, r);
        vec3 R = reflect(-V, N);
        vec4 near = reflected(R, r);
        vec3 spec = mix(env(R, r * (u_envMaxLod - 2.0)), near.rgb, near.a) * (F0 * ab.x + ab.y);
        vec3 diff = env(N, u_envMaxLod - 1.5) * base.rgb * (1.0 - metal);
        c += (diff + spec) * ao;
    } else if (u_studio > 0.0) { // no environment: the studio's light
        vec2 ab = envBrdf(NdV, r);
        vec3 R = reflect(-V, N);
        vec4 near = reflected(R, r);
        c += (mix(studio(R, r), near.rgb, near.a) * (F0 * ab.x + ab.y) + studio(N, 1.0) * 0.3 * base.rgb * (1.0 - metal)) * ao;
    } else {
        vec2 ab = envBrdf(NdV, r);
        vec4 near = reflected(reflect(-V, N), r);
        c += near.rgb * near.a * (F0 * ab.x + ab.y) * ao;
    }
    for (int i = 0; i < 8; ++i) {
        if (i >= u_count) break;
        vec3 L; float att = 1.0;
        if (u_type[i] == 0) L = normalize(-u_ldir[i]);
        else {
            vec3 d = u_lpos[i] - v_pos;
            float dist2 = max(dot(d, d), 1e-4);
            L = d * inversesqrt(dist2);
            att = 1.0 / dist2;
            if (u_type[i] == 2) att *= 1.0 - smoothstep(u_cosIn[i], u_cos[i], acos(clamp(dot(-L, normalize(u_ldir[i])), -1.0, 1.0))); // by its angle: an even gradient
        }
        float NdL = max(dot(N, L), 0.0);
        if (NdL <= 0.0) continue;
        vec3 H = normalize(L + V);
        float NdH = max(dot(N, H), 0.0), VdH = max(dot(V, H), 0.0);
        float dd = NdH * NdH * (a * a - 1.0) + 1.0;
        float D = a * a / (dd * dd);
        float k = (r + 1.0) * (r + 1.0) / 8.0;
        float G = (NdV / (NdV * (1.0 - k) + k)) * (NdL / (NdL * (1.0 - k) + k));
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdH, 5.0);
        vec3 spec = D * G * F / (4.0 * NdV * NdL + 1e-4);
        vec3 kd = (1.0 - F) * (1.0 - metal);
        c += (kd * base.rgb + spec) * u_color[i] * NdL * att * lit(i, N) * mix(1.0, contact, 0.5);
    }
    vec3 em = u_emissive * (u_hasEm != 0 ? pow(texture(u_emTex, v_uv).rgb, vec3(2.2)) : vec3(1.0));
    c = aces((c + em) * u_exposure);
    // Dithered: no bands in the gradients of a spot on a floor. Premultiplied, as a group's picture
    o = vec4(clamp(pow(c, vec3(1.0 / 2.2)) + (noise() - 0.5) / 255.0, 0.0, 1.0) * base.a, base.a);
}
)";

// Where surfaces meet: from what is seen, how much of the half-sphere over each point its neighbours hide
static const char *kAoFragment = R"(#version 330 core
in vec2 v_ndc; out vec4 o;
uniform sampler2D u_seen; uniform mat4 u_invProj; uniform mat4 u_proj; uniform float u_radius, u_amount; uniform int u_samples;
vec3 at(vec2 uv) {
    vec4 p = u_invProj * vec4(uv * 2.0 - 1.0, texture(u_seen, uv).r * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
void main() {
    vec2 uv = v_ndc * 0.5 + 0.5;
    if (texture(u_seen, uv).r >= 1.0) { o = vec4(1.0); return; }
    vec3 P = at(uv);
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));
    if (dot(N, P) > 0.0) N = -N;
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float hidden = 0.0;
    int count = u_samples;
    for (int k = 0; k < count; ++k) {
        float t = (float(k) + 0.5) / float(count), a = (noise + float(k) * 0.61803399) * 6.2831853;
        float rr = sqrt(t), z = sqrt(max(0.0, 1.0 - t));
        vec3 h = vec3(cos(a) * rr, sin(a) * rr, z);
        vec3 tx = normalize(cross(N, abs(N.y) > 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0))), ty = cross(N, tx);
        vec3 s = P + (tx * h.x + ty * h.y + N * h.z) * u_radius * mix(0.2, 1.0, t);
        vec4 q = u_proj * vec4(s, 1.0);
        vec2 suv = q.xy / q.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.y < 0.0 || suv.x > 1.0 || suv.y > 1.0) continue;
        float there = at(suv).z;
        float close = smoothstep(0.0, 1.0, u_radius / max(abs(P.z - there), 1e-4));
        hidden += (there >= s.z + u_radius * 0.03 ? 1.0 : 0.0) * close;
    }
    o = vec4(pow(clamp(1.0 - hidden / float(count), 0.0, 1.0), 1.0 + u_amount * 1.5));
}
)";

// Its noise taken away, across no edge of what is seen
static const char *kAoBlurFragment = R"(#version 330 core
in vec2 v_ndc; out vec4 o;
uniform sampler2D u_ao, u_seen; uniform vec2 u_texel;
void main() {
    vec2 uv = v_ndc * 0.5 + 0.5;
    float depth = texture(u_seen, uv).r, sum = 0.0, weight = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            vec2 at = uv + vec2(x, y) * u_texel;
            float w = 1.0 / (1e-5 + abs(texture(u_seen, at).r - depth) * 2000.0 + 1e-3);
            sum += texture(u_ao, at).r * w;
            weight += w;
        }
    o = vec4(sum / max(weight, 1e-5));
}
)";

// The environment behind the models: its direction from the camera, through the inverse of its matrices
static const char *kSkyVertex = R"(#version 330 core
out vec2 v_ndc;
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0; v_ndc = p; gl_Position = vec4(p, 1.0, 1.0); }
)";
static const char *kSkyFragment = R"(#version 330 core
in vec2 v_ndc; out vec4 o;
uniform mat4 u_inv; uniform vec3 u_eye; uniform sampler2D u_env; uniform float u_envIntensity, u_envRot, u_lod, u_exposure;
uniform int u_envLinear;
const float PI = 3.14159265;
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
void main() {
    vec4 p = u_inv * vec4(v_ndc, 1.0, 1.0);
    vec3 d = normalize(p.xyz / p.w - u_eye);
    float c = cos(u_envRot), s = sin(u_envRot);
    d = vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
    vec2 uv = vec2(0.5 + atan(d.x, -d.z) / (2.0 * PI), acos(clamp(d.y, -1.0, 1.0)) / PI);
    vec3 e = textureLod(u_env, vec2(uv.x, 1.0 - uv.y), u_lod).rgb;
    e = (u_envLinear != 0 ? e : pow(e, vec3(2.2))) * u_envIntensity;
    o = vec4(pow(aces(e * u_exposure), vec3(1.0 / 2.2)), 1.0);
}
)";

bool Model3D::uploadGl()
{
    auto f = gl();
    for (Mesh &m : m_meshes) {
        f->glGenVertexArrays(1, &m.vao);
        f->glBindVertexArray(m.vao);
        f->glGenBuffers(1, &m.vbo);
        f->glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
        f->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m.vertices.size() * sizeof(Vertex)), m.vertices.data(), GL_STATIC_DRAW);
        f->glGenBuffers(1, &m.ibo);
        f->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        f->glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(m.indices.size() * sizeof(quint32)), m.indices.data(), GL_STATIC_DRAW);
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, p)));
        f->glEnableVertexAttribArray(1);
        f->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, n)));
        f->glEnableVertexAttribArray(2);
        f->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, uv)));
        f->glBindVertexArray(0);
    }
    m_textures.assign(m_images.size(), 0);
    for (size_t k = 0; k < m_images.size(); ++k) {
        const QImage &img = m_images[k];
        f->glGenTextures(1, &m_textures[k]);
        f->glBindTexture(GL_TEXTURE_2D, m_textures[k]);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width(), img.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
        f->glGenerateMipmap(GL_TEXTURE_2D);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    f->glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

QVector3D LightSettings::pathPoint(double around) const
{
    const QVector3D middle(position[0], position[1], position[2]);
    const double t = around - std::floor(around);
    const float w = std::abs(pathWidth), d = std::abs(pathDepth);
    QVector3D local;
    if (pathShape == 1) {
        const double a = t * 2 * M_PI;
        local = QVector3D(float(std::cos(a)) * w / 2, 0, float(std::sin(a)) * d / 2);
    } else if (pathShape == 2) { // its four sides, at the same pace
        float left = float(t) * 2 * (w + d);
        if (left < w) local = QVector3D(left - w / 2, 0, -d / 2);
        else if ((left -= w) < d) local = QVector3D(w / 2, 0, left - d / 2);
        else if ((left -= d) < w) local = QVector3D(w / 2 - left, 0, d / 2);
        else local = QVector3D(-w / 2, 0, d / 2 - (left - w));
    } else {
        return middle;
    }
    QMatrix4x4 turn;
    turn.rotate(pathRotation[2], 0, 0, 1);
    turn.rotate(pathRotation[1], 0, 1, 0);
    turn.rotate(pathRotation[0], 1, 0, 0);
    return middle + turn.mapVector(local);
}

QVector3D LightSettings::located() const { return pathPoint(double(pathPosition) + pathTravel); }

std::vector<QVector3D> LightSettings::pathOutline() const
{
    std::vector<QVector3D> points;
    if (pathShape == 1) {
        for (int k = 0; k <= 64; ++k) points.push_back(pathPoint(k / 64.0));
    } else if (pathShape == 2) {
        const double w = std::abs(pathWidth), d = std::abs(pathDepth), all = std::max(1e-6, 2 * (w + d));
        for (const double along : {0.0, w, w + d, 2 * w + d}) points.push_back(pathPoint(along / all));
        points.push_back(points.front());
    }
    return points;
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

void Model3D::drawMeshes(GLuint program, GLuint picture, int pass, const ModelPlacement *look)
{
    auto f = gl();
    if (!m_uploaded) {
        m_uploaded = true;
        uploadGl();
    }
    auto loc = [&](const char *name) { return f->glGetUniformLocation(program, name); };
    auto image = [&](int index) -> GLuint { return index >= 0 && size_t(index) < m_textures.size() ? m_textures[size_t(index)] : 0; };
    // Its maps on units 3 to 7 (0: its base colour, 1: the displacement, 2: the environment)
    struct Map {
        const char *sampler, *has;
        int unit;
    };
    static const Map maps[5] = {{"u_roughTex", "u_hasRough", 3}, {"u_metalTex", "u_hasMetal", 4}, {"u_normalTex", "u_hasNormal", 5},
                                {"u_aoTexMap", "u_hasAo", 6}, {"u_emTex", "u_hasEm", 7}};
    for (const Map &map : maps) f->glUniform1i(loc(map.sampler), map.unit);
    for (const Mesh &m : m_meshes) {
        if (!m.vao) continue;
        Material mat = m_materials.empty() ? Material() : m_materials[size_t(std::clamp(m.material, 0, int(m_materials.size()) - 1))];
        if (m_primitive && look) { // a primitive: the material its parameters give it
            // A colour chosen as it looks; the materials are in linear light
            mat.baseColor = {{std::pow(look->color[0], 2.2f), std::pow(look->color[1], 2.2f), std::pow(look->color[2], 2.2f), 1}};
            mat.metallic = look->metal;
            mat.roughness = look->rough;
        }
        if (picture) { // a layer's picture instead of its colours (a white material under it)
            mat.baseColor = {{1, 1, 1, 1}};
            mat.emissive = {{0, 0, 0}};
            mat.emTex = -1;
            mat.alphaMode = 0;
        }
        if ((mat.alphaMode == 2) != (pass == 1)) continue; // the blended ones after the others
        f->glUniform4f(loc("u_base"), mat.baseColor[0], mat.baseColor[1], mat.baseColor[2], mat.baseColor[3]);
        f->glUniform1f(loc("u_metal"), mat.metallic);
        f->glUniform1f(loc("u_rough"), mat.roughness);
        f->glUniform3f(loc("u_emissive"), mat.emissive[0], mat.emissive[1], mat.emissive[2]);
        f->glUniform1i(loc("u_alphaMode"), picture ? 2 : mat.alphaMode); // a picture keeps its alpha
        f->glUniform1f(loc("u_alphaCutoff"), mat.alphaCutoff);
        f->glUniform1f(loc("u_normalScale"), mat.normalScale);
        f->glUniform1f(loc("u_aoStrength"), mat.aoStrength);
        auto channel = [](int c) { return QVector4D(c == 0, c == 1, c == 2, c == 3); };
        const QVector4D rc = channel(mat.roughChannel), mc = channel(mat.metalChannel);
        f->glUniform4f(loc("u_roughCh"), rc.x(), rc.y(), rc.z(), rc.w());
        f->glUniform4f(loc("u_metalCh"), mc.x(), mc.y(), mc.z(), mc.w());
        const int indices[5] = {mat.roughTex, mat.metalTex, mat.normalTex, mat.aoTex, mat.emTex};
        for (int k = 0; k < 5; ++k) {
            const GLuint t = image(indices[k]);
            f->glUniform1i(loc(maps[k].has), t ? 1 : 0);
            f->glActiveTexture(GL_TEXTURE0 + GLenum(maps[k].unit));
            f->glBindTexture(GL_TEXTURE_2D, t);
        }
        f->glActiveTexture(GL_TEXTURE0);
        const bool textured = picture || image(mat.texture);
        f->glUniform1i(loc("u_hasTex"), textured ? 1 : 0);
        f->glUniform1i(loc("u_layerTex"), picture ? 1 : 0);
        f->glBindTexture(GL_TEXTURE_2D, picture ? picture : image(mat.texture));
        f->glBindVertexArray(m.vao);
        f->glDrawElements(GL_TRIANGLES, GLsizei(m.indices.size()), GL_UNSIGNED_INT, nullptr);
    }
    f->glBindVertexArray(0);
    for (const Map &map : maps) {
        f->glActiveTexture(GL_TEXTURE0 + GLenum(map.unit));
        f->glBindTexture(GL_TEXTURE_2D, 0);
    }
    f->glActiveTexture(GL_TEXTURE0);
    f->glBindTexture(GL_TEXTURE_2D, 0);
}

void Model3D::setDetail(int detail)
{
    detail = std::clamp(detail, 1, 8);
    if (!m_primitive || detail == m_detail) return;
    m_detail = detail;
    releaseMeshes(); // render thread: made again, uploaded when next drawn
    m_meshes.clear();
    QString err;
    loadPrimitive(m_path.section('/', -1), &err);
    computeBounds();
}

void Model3D::releaseMeshes()
{
    if (!QOpenGLContext::currentContext()) return;
    auto f = gl();
    for (Mesh &m : m_meshes) {
        if (m.vao) f->glDeleteVertexArrays(1, &m.vao);
        if (m.vbo) f->glDeleteBuffers(1, &m.vbo);
        if (m.ibo) f->glDeleteBuffers(1, &m.ibo);
        m.vao = m.vbo = m.ibo = 0;
    }
    for (GLuint t : m_textures) f->glDeleteTextures(1, &t);
    m_textures.clear();
    m_uploaded = false;
}

void Model3D::releaseGl()
{
    if (!QOpenGLContext::currentContext()) return;
    auto f = gl();
    for (Mesh &m : m_meshes) {
        if (m.vao) f->glDeleteVertexArrays(1, &m.vao);
        if (m.vbo) f->glDeleteBuffers(1, &m.vbo);
        if (m.ibo) f->glDeleteBuffers(1, &m.ibo);
        m.vao = m.vbo = m.ibo = 0;
    }
    for (GLuint t : m_textures) f->glDeleteTextures(1, &t);
    m_textures.clear();
    m_uploaded = false;
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

bool Scene3D::copyEnvironment(GLuint source)
{
    auto f = gl();
    if (!m_env) { // its copy, in floats, with its blurred levels (the rougher a surface, the blurrier its reflection)
        f->glGenTextures(1, &m_env);
        f->glBindTexture(GL_TEXTURE_2D, m_env);
        for (int level = 0; (kEnvW >> level) > 0; ++level) // OpenGL 4.1 (macOS): each level allocated
            f->glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA16F, std::max(1, kEnvW >> level), std::max(1, kEnvH >> level), 0, GL_RGBA,
                            GL_HALF_FLOAT, nullptr);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); // around: no seam
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->glGenFramebuffers(1, &m_envFbo);
        f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_envFbo);
        f->glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_env, 0);
        f->glGenFramebuffers(1, &m_readFbo);
    }
    // The layer's picture, its size whatever it is, into the copy; then its levels
    GLint sw = 0, sh = 0;
    f->glBindTexture(GL_TEXTURE_2D, source);
    f->glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &sw);
    f->glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &sh);
    if (sw <= 0 || sh <= 0) return false;
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFbo);
    f->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source, 0);
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_envFbo);
    f->glBlitFramebuffer(0, 0, sw, sh, 0, 0, kEnvW, kEnvH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    f->glBindTexture(GL_TEXTURE_2D, m_env);
    f->glGenerateMipmap(GL_TEXTURE_2D);
    f->glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool Scene3D::lite(const QString &renderer)
{
    return renderer.contains(QLatin1String("Apple Software Renderer")) || qEnvironmentVariableIsSet("NYXMAP_3D_LITE");
}

GLuint Scene3D::draw(const SceneSettings &s, const std::vector<Item> &items, const std::vector<Lamp> &lamps, int w, int h,
                     GLuint environment, bool environmentLinear, const Scene3DQuality &quality)
{
    if (w <= 0 || h <= 0) return 0;
    auto f = gl();
    if (!m_compiled) {
        m_compiled = true;
        m_lite = lite(QString::fromLatin1(reinterpret_cast<const char *>(f->glGetString(GL_RENDERER))));
        QString log;
        m_program = compileProgram(QString::fromLatin1(kVertex), QString::fromLatin1(kFragment), &log);
        if (!m_program) qWarning("3D scene shader: %s", qPrintable(log));
        m_shadowProgram = compileProgram(QString::fromLatin1(kVertex), QStringLiteral("#version 330 core\nvoid main() {}\n"), &log);
        if (!m_shadowProgram) qWarning("3D shadow shader: %s", qPrintable(log));
        m_pointProgram = compileProgram(QString::fromLatin1(kVertex),
                                        QStringLiteral("#version 330 core\nin vec3 v_pos; uniform vec3 u_light; uniform float u_far;\n"
                                                       "void main() { gl_FragDepth = length(v_pos - u_light) / u_far; }\n"), &log);
        if (!m_pointProgram) qWarning("3D point shadow shader: %s", qPrintable(log));
        m_aoProgram = compileProgram(QString::fromLatin1(kSkyVertex), QString::fromLatin1(kAoFragment), &log);
        if (!m_aoProgram) qWarning("3D occlusion shader: %s", qPrintable(log));
        m_aoBlurProgram = compileProgram(QString::fromLatin1(kSkyVertex), QString::fromLatin1(kAoBlurFragment), &log);
        if (!m_aoBlurProgram) qWarning("3D occlusion blur shader: %s", qPrintable(log));
        m_sky = compileProgram(QString::fromLatin1(kSkyVertex), QString::fromLatin1(kSkyFragment), &log);
        if (!m_sky) qWarning("3D environment shader: %s", qPrintable(log));
        f->glGenVertexArrays(1, &m_skyVao);
    }
    const bool hasEnv = environment && copyEnvironment(environment);
    const float envMaxLod = std::log2(float(kEnvW));
    if (!m_program) return 0;
    const int aaLevel = m_lite ? 0 : std::clamp(quality.antialiasing, 0, 3);
    GLint maxSamples = 1;
    f->glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    const int wantedSamples = std::clamp(1 << aaLevel, 1, std::max(1, maxSamples));
    if (!m_fbo || w != m_w || h != m_h || wantedSamples != m_samples) { // its targets, the composition's size: antialiased with depth, and its picture
        if (m_fbo) {
            f->glDeleteFramebuffers(1, &m_fbo);
            f->glDeleteFramebuffers(1, &m_msFbo);
            f->glDeleteTextures(1, &m_color);
            f->glDeleteRenderbuffers(1, &m_msColor);
            f->glDeleteRenderbuffers(1, &m_depth);
            f->glDeleteFramebuffers(1, &m_preFbo);
            f->glDeleteTextures(1, &m_preDepth);
            f->glDeleteFramebuffers(1, &m_aoFbo);
            f->glDeleteTextures(2, m_aoTex);
        }
        m_hasPicture = false;
        m_w = w;
        m_h = h;
        f->glGenTextures(1, &m_color);
        f->glBindTexture(GL_TEXTURE_2D, m_color);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glGenFramebuffers(1, &m_fbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);
        m_samples = wantedSamples;
        const GLsizei samples = wantedSamples;
        f->glGenRenderbuffers(1, &m_msColor);
        f->glBindRenderbuffer(GL_RENDERBUFFER, m_msColor);
        f->glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
        f->glGenRenderbuffers(1, &m_depth);
        f->glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
        f->glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
        f->glGenFramebuffers(1, &m_msFbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_msFbo);
        f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msColor);
        f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
        // The depth of what is seen, and the occlusion it makes at half its size
        f->glGenTextures(1, &m_preDepth);
        f->glBindTexture(GL_TEXTURE_2D, m_preDepth);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->glGenFramebuffers(1, &m_preFbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_preFbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_preDepth, 0);
        const GLenum noColor = GL_NONE;
        f->glDrawBuffers(1, &noColor);
        f->glReadBuffer(GL_NONE);
        f->glGenTextures(2, m_aoTex);
        for (GLuint tex : m_aoTex) {
            f->glBindTexture(GL_TEXTURE_2D, tex);
            f->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, std::max(1, w / 2), std::max(1, h / 2), 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glGenFramebuffers(1, &m_aoFbo);
    }

    // The lights' views of the models: what each shadow is read from
    QMatrix4x4 shadowMatrices[kMaxShadows];
    float shadowRadii[kMaxShadows] = {}, shadowBiases[kMaxShadows] = {};
    GLint shadowOf[kMaxLights], pointOf[kMaxLights];
    std::fill(std::begin(shadowOf), std::end(shadowOf), -1);
    std::fill(std::begin(pointOf), std::end(pointOf), -1);
    int shadows = 0, pointShadows = 0;
    // Its shadow maps, as sharp as its quality asks
    const int shadowLevel = m_lite ? 0 : std::clamp(quality.shadows, 0, 3);
    const int shadowSize = shadowLevel ? 512 << shadowLevel : 0, pointSize = shadowSize / 2;
    if (shadowSize && shadowSize != m_shadowSize) { // made again at their new size
        if (m_shadowMaps) f->glDeleteTextures(1, &m_shadowMaps);
        f->glDeleteTextures(kMaxPointShadows, m_pointMaps);
        m_shadowMaps = m_pointMaps[0] = m_pointMaps[1] = 0;
        m_shadowSize = shadowSize;
        m_shadowKey.clear();
    }
    QVector3D pointAt[kMaxPointShadows];
    float pointFar[kMaxPointShadows] = {}, pointSoft[kMaxPointShadows] = {};
    {
        // All the models, in a sphere
        QVector3D center;
        float weight = 0, reach = 0;
        const auto sphere = [](const Item &item) {
            const QMatrix4x4 m = item.parent * item.model->matrix(item.placement);
            const float size = std::max({m.column(0).toVector3D().length(), m.column(1).toVector3D().length(), m.column(2).toVector3D().length()});
            return std::pair(m.map(item.model->center()), item.model->radius() * size);
        };
        // Leaving out what is far larger than the others: a floor, a wall only receive shadows, and the maps stay sharp
        std::vector<float> sizes;
        for (const Item &item : items)
            if (item.model && !item.placement.unlit) sizes.push_back(sphere(item).second);
        std::sort(sizes.begin(), sizes.end());
        const float largest = sizes.empty() ? 0.0f : sizes[sizes.size() / 2] * 4;
        const auto casts = [&](const Item &item) { return item.model && !item.placement.unlit && sphere(item).second <= largest; };
        for (const Item &item : items)
            if (casts(item)) {
                center += sphere(item).first;
                weight += 1;
            }
        if (weight > 0) center /= weight;
        for (const Item &item : items)
            if (casts(item)) {
                const auto [c, r] = sphere(item);
                reach = std::max(reach, (c - center).length() + r);
            }
        int lightIndex = 0;
        for (const Lamp &lamp : lamps) {
            const LightSettings &l = lamp.light;
            if (l.intensity * lamp.gain <= 0 || lightIndex >= kMaxLights) continue;
            const int k = lightIndex++;
            if (!shadowLevel || !l.shadow || reach <= 0 || !m_shadowProgram) continue;
            const QVector3D at(l.position[0], l.position[1], l.position[2]), dir = Camera3D::aim(l.yaw, l.pitch);
            if (l.type == 1) { // a point: the distances all around it
                if (pointShadows >= kMaxPointShadows || !m_pointProgram) continue;
                const int slot = pointShadows++;
                pointOf[k] = slot;
                pointAt[slot] = at;
                pointFar[slot] = std::max((center - at).length() + reach * 3, 1.0f);
                pointSoft[slot] = std::max(0.0f, l.softness);
                continue;
            }
            if (shadows >= kMaxShadows) continue;
            QMatrix4x4 lightView, lightProj;
            float across; // the width its map covers where the models are
            if (l.type == 0) {
                const QVector3D up = std::abs(dir.y()) > 0.95f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
                lightView.lookAt(center - dir * reach * 2.5f, center, up);
                lightProj.ortho(-reach, reach, -reach, reach, reach * 0.1f, reach * 5);
                across = reach * 2;
            } else {
                const float away = (center - at).length();
                const QVector3D aim = dir;
                const float half = std::clamp(l.angle + 2.0f, 2.0f, 80.0f);
                const QVector3D up = std::abs(aim.y()) > 0.95f ? QVector3D(0, 0, 1) : QVector3D(0, 1, 0);
                lightView.lookAt(at, at + aim, up);
                const float farPlane = std::max(away + reach * 1.5f, 1.0f);
                lightProj.perspective(half * 2, 1, std::max(0.05f, farPlane * 0.002f), farPlane);
                across = 2 * std::max(away, 0.1f) * std::tan(qDegreesToRadians(half));
            }
            const int slot = shadows++;
            shadowOf[k] = slot;
            shadowMatrices[slot] = lightProj * lightView;
            const float texel = across / float(shadowSize);
            shadowRadii[slot] = std::max(0.0f, l.softness) * 2.0f / float(shadowSize);
            shadowBiases[slot] = texel * 1.5f + across * 0.0008f * std::max(0.0f, l.softness);
        }
        // Its maps drawn again only when what they show has changed. A picture pushing a model's points can change at
        // any frame
        QByteArray key;
        bool still = true;
        const auto addTo = [&key](const void *data, size_t size) { key.append(static_cast<const char *>(data), qsizetype(size)); };
        for (const Item &item : items)
            if (casts(item)) {
                const QMatrix4x4 m = item.parent * item.model->matrix(item.placement);
                const Model3D *which = item.model.get();
                addTo(&which, sizeof which);
                addTo(m.constData(), sizeof(float) * 16);
                addTo(&item.placement.bend, sizeof(float));
                addTo(&item.placement.detail, sizeof(int));
                still = still && !item.displace;
            }
        for (int slot = 0; slot < shadows; ++slot) addTo(shadowMatrices[slot].constData(), sizeof(float) * 16);
        for (int slot = 0; slot < pointShadows; ++slot) {
            addTo(&pointAt[slot], sizeof(QVector3D));
            addTo(&pointFar[slot], sizeof(float));
        }
        addTo(&shadows, sizeof shadows);
        addTo(&pointShadows, sizeof pointShadows);
        const bool again = !still || key != m_shadowKey;
        m_shadowKey = key;
        if (again && (shadows || pointShadows)) ++m_shadowRenders;
        const auto drawCasters = [&](GLuint program) {
            auto where = [&](const char *name) { return f->glGetUniformLocation(program, name); };
            for (const Item &item : items) {
                if (!casts(item)) continue; // a screen gives light, a floor or a wall only receives
                const QMatrix4x4 model = item.parent * item.model->matrix(item.placement);
                f->glUniformMatrix4fv(where("u_model"), 1, GL_FALSE, model.constData());
                f->glUniformMatrix3fv(where("u_normal"), 1, GL_FALSE, model.normalMatrix().constData());
                f->glUniform1f(where("u_bend"), qDegreesToRadians(std::clamp(item.placement.bend, -359.0f, 359.0f)));
                f->glUniform1f(where("u_bendCx"), item.model->centerX());
                f->glUniform1f(where("u_bendW"), item.model->width());
                f->glUniform1i(where("u_hasDisp"), item.displace ? 1 : 0);
                f->glUniform1f(where("u_dispAmount"), item.displaceAmount);
                f->glUniform1f(where("u_dispMid"), item.displaceMid);
                f->glActiveTexture(GL_TEXTURE1);
                f->glBindTexture(GL_TEXTURE_2D, item.displace);
                f->glActiveTexture(GL_TEXTURE0);
                item.model->drawMeshes(program, item.picture, 0, &item.placement);
            }
        };
        if (again && pointShadows) {
            if (!m_shadowFbo) f->glGenFramebuffers(1, &m_shadowFbo);
            f->glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
            const GLenum none = GL_NONE;
            f->glDrawBuffers(1, &none);
            f->glReadBuffer(GL_NONE);
            f->glViewport(0, 0, pointSize, pointSize);
            f->glEnable(GL_DEPTH_TEST);
            f->glDepthFunc(GL_LESS);
            f->glDisable(GL_BLEND);
            f->glDisable(GL_CULL_FACE);
            f->glUseProgram(m_pointProgram);
            auto where = [&](const char *name) { return f->glGetUniformLocation(m_pointProgram, name); };
            f->glUniform1i(where("u_disp"), 1);
            static const QVector3D kLook[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            static const QVector3D kUp[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
            for (int slot = 0; slot < pointShadows; ++slot) {
                if (!m_pointMaps[slot]) {
                    f->glGenTextures(1, &m_pointMaps[slot]);
                    f->glBindTexture(GL_TEXTURE_CUBE_MAP, m_pointMaps[slot]);
                    for (int face = 0; face < 6; ++face)
                        f->glTexImage2D(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GL_DEPTH_COMPONENT24, pointSize, pointSize, 0,
                                        GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
                    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    for (GLenum axis : {GLenum(GL_TEXTURE_WRAP_S), GLenum(GL_TEXTURE_WRAP_T), GLenum(GL_TEXTURE_WRAP_R)})
                        f->glTexParameteri(GL_TEXTURE_CUBE_MAP, axis, GL_CLAMP_TO_EDGE);
                    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                    f->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
                    f->glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
                }
                f->glUniform3f(where("u_light"), pointAt[slot].x(), pointAt[slot].y(), pointAt[slot].z());
                f->glUniform1f(where("u_far"), pointFar[slot]);
                for (int face = 0; face < 6; ++face) {
                    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), m_pointMaps[slot], 0);
                    f->glClear(GL_DEPTH_BUFFER_BIT);
                    QMatrix4x4 faceView, faceProj;
                    faceView.lookAt(pointAt[slot], pointAt[slot] + kLook[face], kUp[face]);
                    faceProj.perspective(90, 1, 0.02f, pointFar[slot]);
                    f->glUniformMatrix4fv(where("u_view"), 1, GL_FALSE, faceView.constData());
                    f->glUniformMatrix4fv(where("u_proj"), 1, GL_FALSE, faceProj.constData());
                    drawCasters(m_pointProgram);
                }
            }
            // The array of maps goes back on this framebuffer below
            f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        }
        if (again && shadows) {
            if (!m_shadowMaps) {
                f->glGenTextures(1, &m_shadowMaps);
                f->glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadowMaps);
                f->glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, shadowSize, shadowSize, kMaxShadows, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
                f->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                f->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                f->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                f->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                f->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                f->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
                f->glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            }
            if (!m_shadowFbo) f->glGenFramebuffers(1, &m_shadowFbo);
            f->glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
            const GLenum none = GL_NONE;
            f->glDrawBuffers(1, &none);
            f->glReadBuffer(GL_NONE);
            f->glViewport(0, 0, shadowSize, shadowSize);
            f->glEnable(GL_DEPTH_TEST);
            f->glDepthFunc(GL_LESS);
            f->glDisable(GL_BLEND);
            f->glDisable(GL_CULL_FACE);
            f->glEnable(GL_POLYGON_OFFSET_FILL);
            f->glPolygonOffset(1.5f, 2.0f);
            f->glUseProgram(m_shadowProgram);
            auto where = [&](const char *name) { return f->glGetUniformLocation(m_shadowProgram, name); };
            const QMatrix4x4 identity;
            f->glUniformMatrix4fv(where("u_view"), 1, GL_FALSE, identity.constData());
            f->glUniform1i(where("u_disp"), 1);
            for (int slot = 0; slot < shadows; ++slot) {
                f->glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadowMaps, 0, slot);
                f->glClear(GL_DEPTH_BUFFER_BIT);
                f->glUniformMatrix4fv(where("u_proj"), 1, GL_FALSE, shadowMatrices[slot].constData());
                drawCasters(m_shadowProgram);
            }
            f->glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }
    // What is seen, nearest first: for the occlusion and the reflections
    const Camera3D cam = Camera3D::of(s, float(w) / float(h));
    const QMatrix4x4 &view = cam.view, &proj = cam.proj;
    const QVector3D eye = cam.eye;
    const int contactLevel = m_lite ? 0 : std::clamp(quality.contact, 0, 3), reflectLevel = m_lite ? 0 : std::clamp(quality.reflections, 0, 3);
    const bool wantsAo = contactLevel && s.ao > 0 && m_aoProgram && m_aoBlurProgram;
    const float reflections = reflectLevel ? s.reflections : 0.0f;
    if (wantsAo || reflections > 0) {
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_preFbo);
        f->glViewport(0, 0, w, h);
        f->glClear(GL_DEPTH_BUFFER_BIT);
        f->glEnable(GL_DEPTH_TEST);
        f->glDepthFunc(GL_LESS);
        f->glDisable(GL_BLEND);
        f->glDisable(GL_CULL_FACE);
        f->glUseProgram(m_shadowProgram);
        auto where = [&](const char *name) { return f->glGetUniformLocation(m_shadowProgram, name); };
        f->glUniformMatrix4fv(where("u_view"), 1, GL_FALSE, view.constData());
        f->glUniformMatrix4fv(where("u_proj"), 1, GL_FALSE, proj.constData());
        f->glUniform1i(where("u_disp"), 1);
        for (const Item &item : items) {
            if (!item.model) continue;
            const QMatrix4x4 model = item.parent * item.model->matrix(item.placement);
            f->glUniformMatrix4fv(where("u_model"), 1, GL_FALSE, model.constData());
            f->glUniformMatrix3fv(where("u_normal"), 1, GL_FALSE, model.normalMatrix().constData());
            f->glUniform1f(where("u_bend"), qDegreesToRadians(std::clamp(item.placement.bend, -359.0f, 359.0f)));
            f->glUniform1f(where("u_bendCx"), item.model->centerX());
            f->glUniform1f(where("u_bendW"), item.model->width());
            f->glUniform1i(where("u_hasDisp"), item.displace ? 1 : 0);
            f->glUniform1f(where("u_dispAmount"), item.displaceAmount);
            f->glUniform1f(where("u_dispMid"), item.displaceMid);
            f->glActiveTexture(GL_TEXTURE1);
            f->glBindTexture(GL_TEXTURE_2D, item.displace);
            f->glActiveTexture(GL_TEXTURE0);
            item.model->drawMeshes(m_shadowProgram, item.picture, 0, &item.placement);
        }
    }
    if (wantsAo) {
        const int aw = std::max(1, w / 2), ah = std::max(1, h / 2);
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_aoFbo);
        f->glViewport(0, 0, aw, ah);
        f->glDisable(GL_DEPTH_TEST);
        f->glBindVertexArray(m_skyVao);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_aoTex[0], 0);
        f->glUseProgram(m_aoProgram);
        auto where = [&](const char *name) { return f->glGetUniformLocation(m_aoProgram, name); };
        const QMatrix4x4 invProj = proj.inverted();
        f->glUniformMatrix4fv(where("u_invProj"), 1, GL_FALSE, invProj.constData());
        f->glUniformMatrix4fv(where("u_proj"), 1, GL_FALSE, proj.constData());
        f->glUniform1f(where("u_radius"), std::max(0.001f, s.aoRadius));
        f->glUniform1f(where("u_amount"), s.ao);
        f->glUniform1i(where("u_samples"), contactLevel == 1 ? 8 : contactLevel == 2 ? 14 : 24);
        f->glUniform1i(where("u_seen"), 0);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, m_preDepth);
        f->glDrawArrays(GL_TRIANGLES, 0, 3);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_aoTex[1], 0);
        f->glUseProgram(m_aoBlurProgram);
        f->glUniform1i(f->glGetUniformLocation(m_aoBlurProgram, "u_seen"), 0);
        f->glUniform1i(f->glGetUniformLocation(m_aoBlurProgram, "u_ao"), 1);
        f->glUniform2f(f->glGetUniformLocation(m_aoBlurProgram, "u_texel"), 1.0f / float(aw), 1.0f / float(ah));
        f->glActiveTexture(GL_TEXTURE1);
        f->glBindTexture(GL_TEXTURE_2D, m_aoTex[0]);
        f->glDrawArrays(GL_TRIANGLES, 0, 3);
        f->glActiveTexture(GL_TEXTURE1);
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, 0);
        f->glBindVertexArray(0);
    }
    f->glBindFramebuffer(GL_FRAMEBUFFER, m_msFbo);
    f->glViewport(0, 0, w, h);
    const float a = s.background[3];
    f->glClearColor(s.background[0] * a, s.background[1] * a, s.background[2] * a, a); // premultiplied
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    f->glEnable(GL_DEPTH_TEST);
    f->glDepthFunc(GL_LEQUAL);
    f->glDisable(GL_BLEND);
    f->glDisable(GL_CULL_FACE);

    const float envRot = qDegreesToRadians(s.envRotation);
    if (hasEnv && s.envShow && m_sky) { // the environment behind them
        f->glDisable(GL_DEPTH_TEST);
        f->glUseProgram(m_sky);
        const QMatrix4x4 inv = (proj * view).inverted();
        f->glUniformMatrix4fv(f->glGetUniformLocation(m_sky, "u_inv"), 1, GL_FALSE, inv.constData());
        f->glUniform3f(f->glGetUniformLocation(m_sky, "u_eye"), eye.x(), eye.y(), eye.z());
        f->glUniform1f(f->glGetUniformLocation(m_sky, "u_envIntensity"), s.envIntensity);
        f->glUniform1f(f->glGetUniformLocation(m_sky, "u_envRot"), envRot);
        f->glUniform1f(f->glGetUniformLocation(m_sky, "u_lod"), std::clamp(s.envBlur, 0.0f, 1.0f) * (envMaxLod - 2));
        f->glUniform1f(f->glGetUniformLocation(m_sky, "u_exposure"), s.exposure);
        f->glUniform1i(f->glGetUniformLocation(m_sky, "u_envLinear"), environmentLinear ? 1 : 0);
        f->glUniform1i(f->glGetUniformLocation(m_sky, "u_env"), 2);
        f->glActiveTexture(GL_TEXTURE2);
        f->glBindTexture(GL_TEXTURE_2D, m_env);
        f->glBindVertexArray(m_skyVao);
        f->glDrawArrays(GL_TRIANGLES, 0, 3);
        f->glBindVertexArray(0);
        f->glActiveTexture(GL_TEXTURE0);
        f->glEnable(GL_DEPTH_TEST);
    }

    f->glUseProgram(m_program);
    auto loc = [&](const char *name) { return f->glGetUniformLocation(m_program, name); };
    f->glUniformMatrix4fv(loc("u_view"), 1, GL_FALSE, view.constData());
    f->glUniformMatrix4fv(loc("u_proj"), 1, GL_FALSE, proj.constData());
    f->glUniform3f(loc("u_eye"), eye.x(), eye.y(), eye.z());
    f->glUniform3f(loc("u_ambient"), s.ambient[0] * s.ambientIntensity, s.ambient[1] * s.ambientIntensity, s.ambient[2] * s.ambientIntensity);
    f->glUniform1f(loc("u_exposure"), s.exposure);
    GLint types[kMaxLights] = {};
    GLfloat colors[3 * kMaxLights] = {}, positions[3 * kMaxLights] = {}, directions[3 * kMaxLights] = {}, cones[kMaxLights] = {}, conesIn[kMaxLights] = {};
    int count = 0;
    for (const Lamp &lamp : lamps) {
        const LightSettings &l = lamp.light;
        const float power = l.intensity * lamp.gain;
        if (power <= 0 || count >= kMaxLights) continue;
        const int k = count++;
        types[k] = std::clamp(l.type, 0, 2);
        const QVector3D d = Camera3D::aim(l.yaw, l.pitch);
        for (int c = 0; c < 3; ++c) {
            colors[3 * k + c] = l.color[size_t(c)] * power;
            positions[3 * k + c] = l.position[size_t(c)];
            directions[3 * k + c] = d[c];
        }
        const float spread = std::clamp(l.angle, 1.0f, 89.0f);
        cones[k] = qDegreesToRadians(spread); // angles, in radians
        conesIn[k] = std::min(cones[k] - 1e-3f, qDegreesToRadians(spread * (1 - std::clamp(l.falloff, 0.0f, 1.0f))));
    }
    f->glUniform1i(loc("u_count"), count);
    f->glUniform1iv(loc("u_type"), kMaxLights, types);
    f->glUniform3fv(loc("u_color"), kMaxLights, colors);
    f->glUniform3fv(loc("u_lpos"), kMaxLights, positions);
    f->glUniform3fv(loc("u_ldir"), kMaxLights, directions);
    f->glUniform1fv(loc("u_cos"), kMaxLights, cones);
    f->glUniform1fv(loc("u_cosIn"), kMaxLights, conesIn);
    f->glUniform1f(loc("u_studio"), std::max(0.0f, s.studio));
    f->glUniform1iv(loc("u_shadow"), kMaxLights, shadowOf);
    for (int slot = 0; slot < shadows; ++slot)
        f->glUniformMatrix4fv(f->glGetUniformLocation(m_program, qPrintable(QStringLiteral("u_shadowMat[%1]").arg(slot))), 1, GL_FALSE, shadowMatrices[slot].constData());
    f->glUniform1fv(loc("u_shadowRadius"), kMaxShadows, shadowRadii);
    f->glUniform1fv(loc("u_shadowBias"), kMaxShadows, shadowBiases);
    f->glUniform1i(loc("u_shadows"), 9);
    f->glActiveTexture(GL_TEXTURE9);
    f->glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadowMaps);
    f->glUniform1iv(loc("u_pointShadow"), kMaxLights, pointOf);
    f->glUniform1fv(loc("u_pointFar"), kMaxPointShadows, pointFar);
    f->glUniform1fv(loc("u_pointSoft"), kMaxPointShadows, pointSoft);
    f->glUniform1i(loc("u_pointMap0"), 10);
    f->glUniform1i(loc("u_pointMap1"), 11);
    for (int slot = 0; slot < kMaxPointShadows; ++slot) {
        f->glActiveTexture(GLenum(GL_TEXTURE10 + slot));
        f->glBindTexture(GL_TEXTURE_CUBE_MAP, m_pointMaps[slot]);
    }
    // Where surfaces meet, and what they reflect: the last frame's picture, where this one's depth says
    const QMatrix4x4 viewProj = proj * view;
    f->glUniformMatrix4fv(loc("u_viewProj"), 1, GL_FALSE, viewProj.constData());
    f->glUniform2f(loc("u_screen"), float(w), float(h));
    f->glUniform2f(loc("u_depthRange"), cam.nearPlane, cam.farPlane);
    f->glUniform1f(loc("u_aoAmount"), wantsAo ? s.ao : 0.0f);
    f->glUniform1f(loc("u_reflections"), reflections);
    f->glUniform1i(loc("u_taps"), shadowLevel == 1 ? 4 : shadowLevel == 3 ? 24 : 12);
    f->glUniform1i(loc("u_steps"), reflectLevel == 1 ? 20 : reflectLevel == 3 ? 80 : 40);
    f->glUniform1f(loc("u_lastExposure"), s.exposure);
    f->glUniform1i(loc("u_hasLast"), m_hasPicture && reflections > 0 ? 1 : 0);
    f->glUniform1i(loc("u_aoTex"), 12);
    f->glUniform1i(loc("u_seen"), 13);
    f->glUniform1i(loc("u_last"), 14);
    f->glActiveTexture(GL_TEXTURE12);
    f->glBindTexture(GL_TEXTURE_2D, m_aoTex[1]);
    f->glActiveTexture(GL_TEXTURE13);
    f->glBindTexture(GL_TEXTURE_2D, m_preDepth);
    f->glActiveTexture(GL_TEXTURE14);
    f->glBindTexture(GL_TEXTURE_2D, m_color);
    f->glUniform1i(loc("u_tex"), 0);
    f->glUniform1i(loc("u_disp"), 1);
    f->glUniform1i(loc("u_env"), 2);
    f->glUniform1i(loc("u_hasEnv"), hasEnv ? 1 : 0);
    f->glUniform1f(loc("u_envIntensity"), s.envIntensity);
    f->glUniform1f(loc("u_envRot"), envRot);
    f->glUniform1f(loc("u_envMaxLod"), envMaxLod);
    f->glUniform1i(loc("u_envLinear"), environmentLinear ? 1 : 0);
    f->glActiveTexture(GL_TEXTURE2);
    f->glBindTexture(GL_TEXTURE_2D, hasEnv ? m_env : 0);
    f->glActiveTexture(GL_TEXTURE0);
    // The opaque surfaces, then the blended ones over them (their depth not written: what is behind them still shows)
    for (int pass = 0; pass < 2; ++pass)
    for (const Item &item : items) {
        if (!item.model) continue;
        const QMatrix4x4 model = item.parent * item.model->matrix(item.placement);
        f->glUniformMatrix4fv(loc("u_model"), 1, GL_FALSE, model.constData());
        f->glUniformMatrix3fv(loc("u_normal"), 1, GL_FALSE, model.normalMatrix().constData());
        const float bend = qDegreesToRadians(std::clamp(item.placement.bend, -359.0f, 359.0f));
        f->glUniform1f(loc("u_bend"), bend);
        f->glUniform1f(loc("u_bendCx"), item.model->centerX());
        f->glUniform1f(loc("u_bendW"), item.model->width());
        f->glUniform1i(loc("u_unlit"), item.placement.unlit ? 1 : 0);
        f->glUniform1i(loc("u_hasDisp"), item.displace ? 1 : 0);
        f->glUniform1f(loc("u_dispAmount"), item.displaceAmount);
        f->glUniform1f(loc("u_dispMid"), item.displaceMid);
        f->glActiveTexture(GL_TEXTURE1);
        f->glBindTexture(GL_TEXTURE_2D, item.displace);
        f->glActiveTexture(GL_TEXTURE0);
        if (pass == 1) {
            f->glEnable(GL_BLEND);
            f->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied
            f->glDepthMask(GL_FALSE);
        }
        item.model->drawMeshes(m_program, item.picture, pass, &item.placement);
        if (pass == 1) {
            f->glDisable(GL_BLEND);
            f->glDepthMask(GL_TRUE);
        }
    }
    for (int unit : {1, 2}) {
        f->glActiveTexture(GL_TEXTURE0 + GLenum(unit));
        f->glBindTexture(GL_TEXTURE_2D, 0);
    }
    f->glActiveTexture(GL_TEXTURE9);
    f->glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    for (int unit : {10, 11}) {
        f->glActiveTexture(GLenum(GL_TEXTURE0 + unit));
        f->glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
    for (int unit : {12, 13, 14}) {
        f->glActiveTexture(GLenum(GL_TEXTURE0 + unit));
        f->glBindTexture(GL_TEXTURE_2D, 0);
    }
    f->glActiveTexture(GL_TEXTURE0);
    f->glDisable(GL_DEPTH_TEST);
    m_hasPicture = true;
    // Its picture, from its antialiased drawing
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msFbo);
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo);
    f->glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    return m_color;
}

void Scene3D::releaseGl()
{
    if (!QOpenGLContext::currentContext()) return;
    auto f = gl();
    if (m_program) f->glDeleteProgram(m_program);
    if (m_sky) f->glDeleteProgram(m_sky);
    if (m_skyVao) f->glDeleteVertexArrays(1, &m_skyVao);
    if (m_env) f->glDeleteTextures(1, &m_env);
    if (m_envFbo) f->glDeleteFramebuffers(1, &m_envFbo);
    if (m_readFbo) f->glDeleteFramebuffers(1, &m_readFbo);
    m_sky = m_skyVao = m_env = m_envFbo = m_readFbo = 0;
    if (m_fbo) f->glDeleteFramebuffers(1, &m_fbo);
    if (m_color) f->glDeleteTextures(1, &m_color);
    if (m_depth) f->glDeleteRenderbuffers(1, &m_depth);
    if (m_msFbo) f->glDeleteFramebuffers(1, &m_msFbo);
    if (m_msColor) f->glDeleteRenderbuffers(1, &m_msColor);
    if (m_shadowProgram) f->glDeleteProgram(m_shadowProgram);
    if (m_shadowFbo) f->glDeleteFramebuffers(1, &m_shadowFbo);
    if (m_shadowMaps) f->glDeleteTextures(1, &m_shadowMaps);
    m_program = m_fbo = m_color = m_depth = m_msFbo = m_msColor = m_shadowProgram = m_shadowFbo = m_shadowMaps = 0;
    if (m_pointProgram) f->glDeleteProgram(m_pointProgram);
    f->glDeleteTextures(kMaxPointShadows, m_pointMaps);
    if (m_preFbo) f->glDeleteFramebuffers(1, &m_preFbo);
    if (m_preDepth) f->glDeleteTextures(1, &m_preDepth);
    if (m_aoProgram) f->glDeleteProgram(m_aoProgram);
    if (m_aoBlurProgram) f->glDeleteProgram(m_aoBlurProgram);
    if (m_aoFbo) f->glDeleteFramebuffers(1, &m_aoFbo);
    f->glDeleteTextures(2, m_aoTex);
    m_pointProgram = m_preFbo = m_preDepth = m_aoProgram = m_aoBlurProgram = m_aoFbo = 0;
    m_pointMaps[0] = m_pointMaps[1] = m_aoTex[0] = m_aoTex[1] = 0;
    m_shadowKey.clear();
    m_hasPicture = false;
    m_compiled = false;
    m_w = m_h = 0;
}
