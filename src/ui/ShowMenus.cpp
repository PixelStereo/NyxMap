#include "ShowMenus.h"

#include "InteractionOutput.h"
#include "LayerTree.h"

#include <QHash>
#include <QSet>

namespace showmenu {

bool Owner::owns(const QString &path) const
{
    switch (kind) {
    case Kind::Layer: return true;
    case Kind::Composition: return !path.startsWith(QLatin1String("interaction/")) && !path.startsWith(QLatin1String("timeline/"));
    case Kind::Interaction: return path.startsWith(QStringLiteral("interaction/%1/").arg(id));
    case Kind::Timeline: return path.startsWith(QStringLiteral("timeline/%1/").arg(id));
    }
    return false;
}

std::vector<ParamInfo> ofOwner(const std::vector<ParamInfo> &all, const Owner &owner)
{
    std::vector<ParamInfo> out;
    const QString separator = QStringLiteral(" › ");
    for (ParamInfo p : all) {
        if (!owner.owns(p.path)) continue;
        for (int drop = owner.labelDrop(); drop > 0; --drop) {
            const int cut = p.label.indexOf(separator);
            if (cut < 0) break;
            p.label.remove(0, cut + separator.size());
        }
        out.push_back(std::move(p));
    }
    return out;
}

void clearMenu(QMenu *menu)
{
    for (QMenu *submenu : menu->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly)) delete submenu;
    menu->clear();
}

namespace {

// A menu for one owner, filled when it opens
void addOwner(QMenu *parent, const QString &title, const Owner &owner, const Fill &fill)
{
    auto *menu = parent->addMenu(title);
    menu->addAction(QStringLiteral("Values"))->setEnabled(false); // so that it opens: filled when it does
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, owner, fill] {
        clearMenu(menu);
        fill(menu, owner);
        if (menu->isEmpty()) menu->addAction(QStringLiteral("No compatible parameters"))->setEnabled(false);
    });
}

struct Node {
    quint64 id = 0, parent = 0;
    QString name;
    bool group = false;
};

// Walks a tree given parent before children: a node that holds others is a menu, the others are leaves
void walk(QMenu *menu, const std::vector<Node> &nodes, const LayerLeaf &leaf, const LayerContainer &container)
{
    QSet<quint64> parents;
    for (const Node &n : nodes) parents.insert(n.parent);
    QHash<quint64, QMenu *> menus;
    menus.insert(0, menu);
    for (const Node &n : nodes) {
        QMenu *into = menus.value(n.parent, menu);
        if (parents.contains(n.id) || n.group) {
            auto *sub = into->addMenu(n.name);
            menus.insert(n.id, sub);
            container(sub, n.id, n.name, n.group);
        } else {
            leaf(into, n.id, n.name);
        }
    }
}

} // namespace

void fillLayerTree(QMenu *menu, Engine *e, const LayerLeaf &leaf, const LayerContainer &container, bool viewports)
{
    std::vector<Node> nodes;
    {
        Engine::Lock lock(&e->mutex());
        QHash<quint64, QString> names;
        for (int i = 0; i < e->layerCount(); ++i)
            if (const auto *layer = e->layer(i)) names.insert(layer->id, layer->name);
        for (const TreeNode &n : e->structure())
            if ((n.kind == TreeNode::Viewport) == viewports) nodes.push_back({n.id, n.parent, names.value(n.id), n.isGroup()});
    }
    walk(menu, nodes, leaf, container);
}

void fillInteractionTree(QMenu *menu, Engine *e, const LayerLeaf &leaf, const LayerContainer &container)
{
    // The groups, each before what it holds (a group's parent first), then the interactions of the top level and of each group
    const auto groups = e->interactionGroups();
    const auto interactions = e->interactions();
    std::vector<Node> nodes;
    std::vector<quint64> pending;
    QSet<quint64> placed{0};
    // Interactions and groups share no id space: groups are menus keyed by a distinct offset
    constexpr quint64 kGroupBase = quint64(1) << 62;
    std::vector<InteractionGroup> rest = groups;
    while (!rest.empty()) {
        bool moved = false;
        for (auto it = rest.begin(); it != rest.end();) {
            if (placed.contains(it->parent)) {
                nodes.push_back({kGroupBase + it->id, it->parent ? kGroupBase + it->parent : 0, it->name, true});
                placed.insert(it->id);
                it = rest.erase(it);
                moved = true;
            } else {
                ++it;
            }
        }
        if (!moved) break; // a cycle: left out
    }
    for (const Interaction &it : interactions)
        nodes.push_back({it.id, it.group ? kGroupBase + it.group : 0, it.name.isEmpty() ? it.label() : it.name, false});
    // An interaction's id never reaches kGroupBase: told apart by it
    const auto realLeaf = [&](QMenu *into, quint64 id, const QString &name) { leaf(into, id, name); };
    const auto realContainer = [&](QMenu *into, quint64 id, const QString &name, bool isGroup) {
        container(into, id >= kGroupBase ? id - kGroupBase : id, name, isGroup);
    };
    walk(menu, nodes, realLeaf, realContainer);
}

void fillOwners(QMenu *menu, Engine *e, const Fill &fill)
{
    addOwner(menu, QStringLiteral("Composition"), Owner::composition(), fill);

    // A node with something in it is a menu whose first entry is its own values; a leaf is its own menu
    const auto leaf = [fill](QMenu *into, quint64 id, const QString &name) { addOwner(into, name, Owner::forLayer(id), fill); };
    const auto container = [fill](QMenu *into, quint64 id, const QString &, bool isGroup) {
        addOwner(into, isGroup ? QStringLiteral("Group Parameters") : QStringLiteral("Parameters"), Owner::forLayer(id), fill);
    };

    auto *viewports = new QMenu(QStringLiteral("Viewports"), menu);
    fillLayerTree(viewports, e, leaf, container, true);
    if (!viewports->isEmpty()) menu->addMenu(viewports);
    else delete viewports;

    auto *layers = new QMenu(QStringLiteral("Layers"), menu);
    fillLayerTree(layers, e, leaf, container, false);
    if (!layers->isEmpty()) menu->addMenu(layers);
    else delete layers;

    auto *interactions = new QMenu(QStringLiteral("Interactions"), menu);
    fillInteractionTree(
        interactions, e,
        [fill](QMenu *into, quint64 id, const QString &name) { addOwner(into, name, {Owner::Kind::Interaction, id}, fill); },
        [](QMenu *, quint64, const QString &, bool) {});
    if (!interactions->isEmpty()) menu->addMenu(interactions);
    else delete interactions;

    auto *timelines = new QMenu(QStringLiteral("Timelines"), menu);
    for (int i = 0; i < e->animationCount(); ++i) {
        const Animation a = e->animation(i);
        addOwner(timelines, a.name.isEmpty() ? QString::number(a.id) : a.name, {Owner::Kind::Timeline, a.id}, fill);
    }
    if (!timelines->isEmpty()) menu->addMenu(timelines);
    else delete timelines;
}

QString valueLabel(Engine *e, quint64 layer, const QString &param, const QString &none)
{
    if (param.isEmpty()) return none;
    if (!layer) {
        const auto output = InteractionOutput::fromPath(param);
        if (output.protocol == "osc") return QStringLiteral("OSC · %1 · %2").arg(output.destination, output.address);
        if (output.protocol == "dmx") {
            const auto patch = e->fixturePatch();
            const auto *entry = patch.findOutput(output.destination.toULongLong());
            return QStringLiteral("DMX · %1 · %2/%3").arg(entry ? entry->name : QStringLiteral("(output gone)")).arg(output.number).arg(output.channel);
        }
        if (output.protocol == "midi") {
            QString name = QStringLiteral("(output gone)");
            for (const auto &entry : e->midiOutputs())
                if (entry.id == output.destination.toULongLong()) { name = entry.name; break; }
            return QStringLiteral("MIDI · %1 · Ch %2 / CC %3").arg(name).arg(output.channel).arg(output.number);
        }
    }
    QString owner = QStringLiteral("Composition");
    if (layer) {
        Engine::Lock lk(&e->mutex());
        const int i = e->indexOfId(layer);
        const Layer *l = i >= 0 ? e->layer(i) : nullptr;
        owner = l ? l->name : QStringLiteral("(layer gone)");
    }
    // An interaction's or a timeline's value carries its owner in its own label
    const bool own = !layer && (param.startsWith(QLatin1String("interaction/")) || param.startsWith(QLatin1String("timeline/")));
    const QString prefix = own ? QString() : owner + QStringLiteral(" › ");
    ParamInfo info;
    if (e->parameterInfo(layer, param, &info)) return prefix + info.label;
    // A whole color is named after its R / G / B / A members
    if (e->parameterInfo(layer, param + QStringLiteral("/r"), &info))
        return prefix + (info.label.endsWith(QStringLiteral(" R")) ? info.label.chopped(2) : info.label);
    for (const auto &point : e->animatablePoints(layer))
        if (point.path == param) return prefix + point.label;
    return prefix + param;
}

} // namespace showmenu
