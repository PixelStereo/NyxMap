#include "Model3D.h"

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
#include <limits>

Model3D::Model3D() = default;
Model3D::~Model3D() = default; // its GL objects go with releaseGl, on the render thread

QStringList Model3D::extensions() { return {QStringLiteral("fbx"), QStringLiteral("gltf"), QStringLiteral("glb"), QStringLiteral("obj")}; }

bool Model3D::isModelFile(const QString &path) { return extensions().contains(QFileInfo(path).suffix().toLower()); }

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
    const bool ok = ext == QLatin1String("gltf") || ext == QLatin1String("glb") ? loadGltf(path, err) : loadUfbx(path, err);
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
        }
        mat.emissive = {{m->emissive_factor[0], m->emissive_factor[1], m->emissive_factor[2]}};
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
out vec3 v_pos; out vec3 v_n; out vec2 v_uv;
void main() {
    vec4 w = u_model * vec4(a_pos, 1.0);
    v_pos = w.xyz; v_n = u_normal * a_n; v_uv = a_uv;
    gl_Position = u_proj * u_view * w;
}
)";

// Physically based (GGX, Smith, Schlick): its base colour (times its texture's), metal, roughness, emission, under the
// ambient light and up to eight lights; tone mapped, sRGB out
static const char *kFragment = R"(#version 330 core
in vec3 v_pos; in vec3 v_n; in vec2 v_uv; out vec4 o;
uniform vec4 u_base; uniform float u_metal, u_rough; uniform vec3 u_emissive;
uniform int u_hasTex; uniform sampler2D u_tex;
uniform vec3 u_eye, u_ambient; uniform float u_exposure;
uniform int u_count; uniform int u_type[8]; uniform vec3 u_color[8], u_lpos[8], u_ldir[8]; uniform float u_cos[8];
void main() {
    vec4 base = u_base;
    if (u_hasTex != 0) { vec4 t = texture(u_tex, v_uv); base *= vec4(pow(t.rgb, vec3(2.2)), t.a); }
    vec3 N = normalize(v_n), V = normalize(u_eye - v_pos);
    if (dot(N, V) < 0.0) N = -N; // both faces lit
    float r = clamp(u_rough, 0.04, 1.0), a = r * r;
    vec3 F0 = mix(vec3(0.04), base.rgb, u_metal);
    vec3 c = u_ambient * base.rgb * (1.0 - 0.7 * u_metal);
    for (int i = 0; i < 8; ++i) {
        if (i >= u_count) break;
        vec3 L; float att = 1.0;
        if (u_type[i] == 0) L = normalize(-u_ldir[i]);
        else {
            vec3 d = u_lpos[i] - v_pos;
            float dist2 = max(dot(d, d), 1e-4);
            L = d * inversesqrt(dist2);
            att = 1.0 / dist2;
            if (u_type[i] == 2) att *= smoothstep(u_cos[i], mix(u_cos[i], 1.0, 0.2), dot(-L, normalize(u_ldir[i])));
        }
        float NdL = max(dot(N, L), 0.0);
        if (NdL <= 0.0) continue;
        vec3 H = normalize(L + V);
        float NdH = max(dot(N, H), 0.0), NdV = max(dot(N, V), 1e-4), VdH = max(dot(V, H), 0.0);
        float dd = NdH * NdH * (a * a - 1.0) + 1.0;
        float D = a * a / (dd * dd);
        float k = (r + 1.0) * (r + 1.0) / 8.0;
        float G = (NdV / (NdV * (1.0 - k) + k)) * (NdL / (NdL * (1.0 - k) + k));
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdH, 5.0);
        vec3 spec = D * G * F / (4.0 * NdV * NdL + 1e-4);
        vec3 kd = (1.0 - F) * (1.0 - u_metal);
        c += (kd * base.rgb + spec) * u_color[i] * NdL * att;
    }
    c = (c + u_emissive) * u_exposure;
    c = c / (1.0 + c);
    o = vec4(pow(c, vec3(1.0 / 2.2)) * base.a, base.a); // premultiplied, as a group's picture
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

QMatrix4x4 Model3D::matrix(const ModelPlacement &p) const
{
    QMatrix4x4 m;
    m.translate(p.position[0], p.position[1], p.position[2]);
    m.rotate(p.rotation[2], 0, 0, 1);
    m.rotate(p.rotation[1], 0, 1, 0);
    m.rotate(p.rotation[0], 1, 0, 0);
    m.scale(p.scale);
    if (p.fit) {
        m.scale(1.0f / m_radius);
        m.translate(-m_center);
    }
    return m;
}

void Model3D::drawMeshes(GLuint program)
{
    auto f = gl();
    if (!m_uploaded) {
        m_uploaded = true;
        uploadGl();
    }
    auto loc = [&](const char *name) { return f->glGetUniformLocation(program, name); };
    for (const Mesh &m : m_meshes) {
        if (!m.vao) continue;
        const Material mat = m_materials.empty() ? Material() : m_materials[size_t(std::clamp(m.material, 0, int(m_materials.size()) - 1))];
        f->glUniform4f(loc("u_base"), mat.baseColor[0], mat.baseColor[1], mat.baseColor[2], mat.baseColor[3]);
        f->glUniform1f(loc("u_metal"), mat.metallic);
        f->glUniform1f(loc("u_rough"), mat.roughness);
        f->glUniform3f(loc("u_emissive"), mat.emissive[0], mat.emissive[1], mat.emissive[2]);
        const bool textured = mat.texture >= 0 && size_t(mat.texture) < m_textures.size();
        f->glUniform1i(loc("u_hasTex"), textured ? 1 : 0);
        f->glBindTexture(GL_TEXTURE_2D, textured ? m_textures[size_t(mat.texture)] : 0);
        f->glBindVertexArray(m.vao);
        f->glDrawElements(GL_TRIANGLES, GLsizei(m.indices.size()), GL_UNSIGNED_INT, nullptr);
    }
    f->glBindVertexArray(0);
    f->glBindTexture(GL_TEXTURE_2D, 0);
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

static QVector3D aim(float yawDeg, float pitchDeg) // where a light or the camera looks from its angles
{
    const float y = qDegreesToRadians(yawDeg), p = qDegreesToRadians(pitchDeg);
    return QVector3D(std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y));
}

GLuint Scene3D::draw(const SceneSettings &s, const std::vector<Item> &items, const std::vector<Lamp> &lamps, int w, int h)
{
    if (w <= 0 || h <= 0) return 0;
    auto f = gl();
    if (!m_compiled) {
        m_compiled = true;
        QString log;
        m_program = compileProgram(QString::fromLatin1(kVertex), QString::fromLatin1(kFragment), &log);
        if (!m_program) qWarning("3D scene shader: %s", qPrintable(log));
    }
    if (!m_program) return 0;
    if (!m_fbo || w != m_w || h != m_h) { // its target: colour and depth, the composition's size
        if (m_fbo) f->glDeleteFramebuffers(1, &m_fbo), f->glDeleteTextures(1, &m_color), f->glDeleteRenderbuffers(1, &m_depth);
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
        f->glGenRenderbuffers(1, &m_depth);
        f->glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
        f->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        f->glGenFramebuffers(1, &m_fbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);
        f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
    }
    f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    f->glViewport(0, 0, w, h);
    const float a = s.background[3];
    f->glClearColor(s.background[0] * a, s.background[1] * a, s.background[2] * a, a); // premultiplied
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    f->glEnable(GL_DEPTH_TEST);
    f->glDepthFunc(GL_LEQUAL);
    f->glDisable(GL_BLEND);
    f->glDisable(GL_CULL_FACE);

    // The camera around its target
    const QVector3D target(s.target[0], s.target[1], s.target[2]);
    const float distance = std::max(0.01f, s.distance);
    const QVector3D eye = target + aim(s.yaw, s.pitch) * distance;
    QMatrix4x4 view, proj;
    view.lookAt(eye, target, QVector3D(0, 1, 0));
    proj.perspective(std::clamp(s.fov, 1.0f, 170.0f), float(w) / float(h), distance * 0.01f, distance * 50.0f + 100.0f);

    f->glUseProgram(m_program);
    auto loc = [&](const char *name) { return f->glGetUniformLocation(m_program, name); };
    f->glUniformMatrix4fv(loc("u_view"), 1, GL_FALSE, view.constData());
    f->glUniformMatrix4fv(loc("u_proj"), 1, GL_FALSE, proj.constData());
    f->glUniform3f(loc("u_eye"), eye.x(), eye.y(), eye.z());
    f->glUniform3f(loc("u_ambient"), s.ambient[0] * s.ambientIntensity, s.ambient[1] * s.ambientIntensity, s.ambient[2] * s.ambientIntensity);
    f->glUniform1f(loc("u_exposure"), s.exposure);
    GLint types[kMaxLights] = {};
    GLfloat colors[3 * kMaxLights] = {}, positions[3 * kMaxLights] = {}, directions[3 * kMaxLights] = {}, cones[kMaxLights] = {};
    int count = 0;
    for (const Lamp &lamp : lamps) {
        const LightSettings &l = lamp.light;
        const float power = l.intensity * lamp.gain;
        if (power <= 0 || count >= kMaxLights) continue;
        const int k = count++;
        types[k] = std::clamp(l.type, 0, 2);
        const QVector3D d = aim(l.yaw, l.pitch);
        for (int c = 0; c < 3; ++c) {
            colors[3 * k + c] = l.color[size_t(c)] * power;
            positions[3 * k + c] = l.position[size_t(c)];
            directions[3 * k + c] = d[c];
        }
        cones[k] = std::cos(qDegreesToRadians(std::clamp(l.angle, 1.0f, 89.0f)));
    }
    f->glUniform1i(loc("u_count"), count);
    f->glUniform1iv(loc("u_type"), kMaxLights, types);
    f->glUniform3fv(loc("u_color"), kMaxLights, colors);
    f->glUniform3fv(loc("u_lpos"), kMaxLights, positions);
    f->glUniform3fv(loc("u_ldir"), kMaxLights, directions);
    f->glUniform1fv(loc("u_cos"), kMaxLights, cones);
    f->glUniform1i(loc("u_tex"), 0);
    f->glActiveTexture(GL_TEXTURE0);
    for (const Item &item : items) {
        if (!item.model) continue;
        const QMatrix4x4 model = item.model->matrix(item.placement);
        f->glUniformMatrix4fv(loc("u_model"), 1, GL_FALSE, model.constData());
        f->glUniformMatrix3fv(loc("u_normal"), 1, GL_FALSE, model.normalMatrix().constData());
        item.model->drawMeshes(m_program);
    }
    f->glDisable(GL_DEPTH_TEST);
    return m_color;
}

void Scene3D::releaseGl()
{
    if (!QOpenGLContext::currentContext()) return;
    auto f = gl();
    if (m_program) f->glDeleteProgram(m_program);
    if (m_fbo) f->glDeleteFramebuffers(1, &m_fbo);
    if (m_color) f->glDeleteTextures(1, &m_color);
    if (m_depth) f->glDeleteRenderbuffers(1, &m_depth);
    m_program = m_fbo = m_color = m_depth = 0;
    m_compiled = false;
    m_w = m_h = 0;
}
