#include "MaterialX.h"
#include "ImageFile.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <cmath>

bool Material3D::setMap(MaterialMap slot, const QString &path, QString *error)
{
    QImage image;
    if (!path.isEmpty()) {
        image = readImageFile(path, -1, error);
        if (image.isNull()) {
            if (error && error->isEmpty()) *error = QStringLiteral("Cannot read material image: %1").arg(path);
            return false;
        }
    }
    paths[slot] = path;
    embeddedMaps[slot].clear();
    images[slot] = std::move(image);
    return true;
}

bool Material3D::setEmbeddedMap(MaterialMap slot, const QImage &image)
{
    QBuffer buffer;
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return false;
    paths[slot].clear();
    images[slot] = image;
    embeddedMaps[slot] = QString::fromLatin1(buffer.data().toBase64());
    return true;
}

namespace {
struct Node {
    QString type;
    QXmlStreamAttributes attrs;
    std::vector<Node> children;
    QString attr(const char *key) const { return attrs.value(QLatin1String(key)).toString(); }
    const Node *find(const QString &name, const QString &type = {}) const
    {
        for (const auto &node : children)
            if (node.attr("name") == name && (type.isEmpty() || node.type == type)) return &node;
        return nullptr;
    }
};

Node readNode(QXmlStreamReader &xml, int depth = 0)
{
    Node node{xml.name().toString(), xml.attributes(), {}};
    if (depth >= 64) {
        xml.raiseError(QStringLiteral("MaterialX document is nested too deeply."));
        return node;
    }
    while (xml.readNextStartElement()) node.children.push_back(readNode(xml, depth + 1));
    return node;
}

const Node *linked(const Node &input, const Node &scope, const Node &root, const Node *&nextScope)
{
    nextScope = &scope;
    if (!input.attr("nodegraph").isEmpty()) {
        nextScope = root.find(input.attr("nodegraph"), QStringLiteral("nodegraph"));
        if (!nextScope) return nullptr;
        const auto *output = nextScope->find(input.attr("output"), QStringLiteral("output"));
        return output ? nextScope->find(output->attr("nodename")) : nullptr;
    }
    if (!input.attr("nodename").isEmpty()) return scope.find(input.attr("nodename"));
    return nullptr;
}

QString value(const Node *input, const Node &scope)
{
    if (!input) return {};
    const QString name = input->attr("interfacename");
    if (const auto *interface = name.isEmpty() ? nullptr : scope.find(name, QStringLiteral("input")))
        return interface->attr("value");
    return input->attr("value");
}

float number(const Node *input, const Node &scope, float fallback)
{
    bool ok = false;
    const float result = value(input, scope).toFloat(&ok);
    return ok && std::isfinite(result) ? result : fallback;
}

const Node *imageNode(const Node &input, const Node &scope, const Node &root, const Node *&imageScope, int depth = 0)
{
    if (depth >= 16) return nullptr;
    const Node *nextScope;
    const auto *node = linked(input, scope, root, nextScope);
    if (!node) return nullptr;
    if (node->type == QLatin1String("image") || node->type == QLatin1String("tiledimage")) {
        imageScope = nextScope;
        return node;
    }
    if (node->type == QLatin1String("normalmap") || node->type == QLatin1String("displacement")) {
        const auto *source = node->find(node->type == QLatin1String("normalmap") ? "in" : "displacement");
        if (source) return imageNode(*source, *nextScope, root, imageScope, depth + 1);
    }
    return nullptr;
}
}

bool importMaterialX(const QString &path, Material3D &material, QString *error)
{
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    QXmlStreamReader xml(&file);
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("materialx"))
        return fail(QStringLiteral("Expected a MaterialX document."));
    const Node root = readNode(xml);
    while (!xml.atEnd()) xml.readNext();
    if (xml.hasError()) return fail(xml.errorString());

    const Node *surface = nullptr, *binding = nullptr;
    for (const auto &node : root.children) {
        if (node.type != QLatin1String("surfacematerial")) continue;
        const auto *input = node.find(QStringLiteral("surfaceshader"));
        const Node *scope;
        if (input) surface = linked(*input, root, root, scope);
        if (surface && surface->type == QLatin1String("standard_surface")) { binding = &node; break; }
        surface = nullptr;
    }
    if (!surface)
        for (const auto &node : root.children)
            if (node.type == QLatin1String("standard_surface")) { surface = &node; break; }
    if (!surface) return fail(QStringLiteral("This importer requires a Standard Surface shader."));

    Material3D result;
    result.id = material.id;
    result.name = surface->attr("name");
    result.color = {{1, 1, 1}};
    result.rough = .2f;
    const QDir folder = QFileInfo(path).absoluteDir();
    auto load = [&](MaterialMap slot, const Node &image, const Node &scope) {
        const QString filename = value(image.find(QStringLiteral("file")), scope);
        if (filename.isEmpty()) return fail(QStringLiteral("An image node has no file."));
        if (!result.setMap(slot, folder.absoluteFilePath(filename), error)) return false;
        if (const auto *tiling = image.find(QStringLiteral("uvtiling"))) {
            const auto *constant = scope.find(tiling->attr("nodename"), QStringLiteral("constant"));
            const auto xy = (constant ? value(constant->find(QStringLiteral("value")), scope) : value(tiling, scope)).split(',');
            if (xy.size() == 2 && xy[0].toFloat() > 0 && xy[1].toFloat() > 0)
                result.uvTiling = {{std::clamp(xy[0].toFloat(), .01f, 100.f), std::clamp(xy[1].toFloat(), .01f, 100.f)}};
        }
        return true;
    };
    auto readSlot = [&](const char *name, MaterialMap slot, float *factor, std::array<float, 3> *color) {
        bool unsupported = false;
        for (const auto &input : surface->children) {
            if (input.attr("name") != QLatin1String(name)) continue;
            const Node *scope;
            if (const auto *image = imageNode(input, root, root, scope)) {
                if (!load(slot, *image, *scope)) return false;
                if (factor) *factor = 1;
                if (color) *color = {{1, 1, 1}};
                if (slot == NormalMap) {
                    const Node *normalScope;
                    const auto *normal = linked(input, root, root, normalScope);
                    if (normal && normal->type == QLatin1String("normalmap"))
                        result.normalStrength = number(normal->find(QStringLiteral("scale")), *normalScope, 1);
                }
                return true;
            }
            unsupported |= !input.attr("nodegraph").isEmpty() || !input.attr("nodename").isEmpty();
            if (factor) *factor = number(&input, root, *factor);
            const auto rgb = input.attr("value").split(',');
            if (color && rgb.size() == 3)
                for (int i = 0; i < 3; ++i) (*color)[i] = std::pow(std::clamp(rgb[i].toFloat(), 0.f, 1.f), 1.f / 2.2f);
        }
        if (unsupported && slot != BaseColorMap)
            return fail(QStringLiteral("Unsupported MaterialX connection on %1.").arg(QString::fromLatin1(name)));
        return true;
    };
    if (!readSlot("base_color", BaseColorMap, nullptr, &result.color) ||
        !readSlot("metalness", MetallicMap, &result.metal, nullptr) ||
        !readSlot("specular_roughness", RoughnessMap, &result.rough, nullptr) ||
        !readSlot("normal", NormalMap, nullptr, nullptr) ||
        !readSlot("emission_color", EmissionMap, nullptr, &result.emissionColor)) return false;

    // Poliigon connects the AO image through a mix over white before multiplying the base color.
    for (const auto &input : surface->children) {
        if (input.attr("name") != QLatin1String("base_color")) continue;
        const Node *scope;
        const auto *multiply = linked(input, root, root, scope);
        if (!multiply || multiply->type != QLatin1String("multiply")) continue;
        const auto *second = multiply->find(QStringLiteral("in2"));
        if (!second) continue;
        const Node *mixScope;
        const auto *mix = linked(*second, *scope, root, mixScope);
        if (!mix || mix->type != QLatin1String("mix")) continue;
        const auto *fg = mix->find(QStringLiteral("fg"));
        const Node *imageScope;
        if (fg)
            if (const auto *image = imageNode(*fg, *mixScope, root, imageScope)) {
                if (!load(OcclusionMap, *image, *imageScope)) return false;
                result.occlusionStrength = number(mix->find(QStringLiteral("mix")), *mixScope, 1);
            }
        if (result.paths[BaseColorMap].isEmpty())
            if (const auto *first = multiply->find(QStringLiteral("in1")))
                if (const auto *image = imageNode(*first, *scope, root, imageScope)) {
                    if (!load(BaseColorMap, *image, *imageScope)) return false;
                    result.color = {{1, 1, 1}};
                }
        break;
    }
    if (binding)
        if (const auto *input = binding->find(QStringLiteral("displacementshader"))) {
            const Node *scope;
            const auto *displacement = linked(*input, root, root, scope);
            const Node *imageScope;
            if (displacement && displacement->type == QLatin1String("displacement"))
                if (const auto *image = imageNode(*input, root, root, imageScope)) {
                    if (!load(HeightMap, *image, *imageScope)) return false;
                    result.height = number(displacement->find(QStringLiteral("scale")), *scope, 0);
                }
        }
    result.emission = number(surface->find(QStringLiteral("emission")), root, 0);
    result.ior = number(surface->find(QStringLiteral("specular_IOR")), root, 1.5f);
    result.specular = number(surface->find(QStringLiteral("specular")), root, 1);
    for (const char *layer : {"transmission", "subsurface", "sheen", "coat", "thin_film_thickness", "specular_anisotropy"}) {
        const auto *input = surface->find(QString::fromLatin1(layer));
        if (input && (number(input, root, 0) != 0 || !input->attr("nodegraph").isEmpty() || !input->attr("nodename").isEmpty()))
            return fail(QStringLiteral("This material uses an unsupported Standard Surface input: %1.").arg(QString::fromLatin1(layer)));
    }
    result.ior = std::clamp(result.ior, 1.f, 4.f);
    result.specular = std::clamp(result.specular, 0.f, 1.f);
    result.normalStrength = std::clamp(result.normalStrength, 0.f, 10.f);
    result.occlusionStrength = std::clamp(result.occlusionStrength, 0.f, 1.f);
    result.height = std::clamp(result.height, -2.f, 2.f);
    result.emission = std::clamp(result.emission, 0.f, 100.f);
    material = std::move(result);
    return true;
}
