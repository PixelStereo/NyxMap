#pragma once
// The one way the interface lets the operator pick a value of the show — an interaction's source, its targets, what an
// action sets, a timeline track's target — and a picture to look at (the viewer). Every such menu starts at the same roots,
// each with its elements below it, in the order of the show:
//
//     Composition ▸ its values
//     Viewports   ▸ each viewport ▸ its values
//     Layers      ▸ the layers as the bin has them (groups, 3D scenes with their objects) ▸ each layer ▸ its values
//     Interactions▸ the interactions as their list has them (groups) ▸ each interaction ▸ the values in play
//     Timelines   ▸ each timeline ▸ its transport, speed, loop mode, its oscillators' period, amplitude…
//
// What a menu does with an owner is its own business (it says which values it takes, and what choosing one does): it is
// given the owner's menu to fill. Nothing else builds these menus.

#include "Engine.h"

#include <QMenu>
#include <QString>
#include <functional>
#include <vector>

namespace showmenu {

// Whose values they are. The composition's own list (Engine::parameters(0)) holds the composition's values, the
// interactions' ("interaction/<id>/…") and the timelines' ("timeline/<id>/…"): an owner picks its own out of it.
struct Owner {
    enum class Kind { Composition, Layer, Interaction, Timeline };
    Kind kind = Kind::Composition;
    quint64 id = 0; // the layer's, the interaction's or the timeline's
    // The id to ask the engine's lists with (parameters(), animatablePoints()…): the layer's, else 0
    quint64 layer() const { return kind == Kind::Layer ? id : 0; }
    bool owns(const QString &path) const;
    // How many leading " › " parts of a label name the owner itself (not shown under its own menu)
    int labelDrop() const { return kind == Kind::Interaction ? 2 : kind == Kind::Timeline ? 1 : 0; }
    static Owner composition() { return {}; }
    static Owner forLayer(quint64 id) { return {id ? Kind::Layer : Kind::Composition, id}; }
};

// The owner's values among `all` (a list of the engine for owner.layer()), with labels from under the owner
std::vector<ParamInfo> ofOwner(const std::vector<ParamInfo> &all, const Owner &owner);

// What to put in the menu of one owner
using Fill = std::function<void(QMenu *into, const Owner &owner)>;
// The roots and the owners below them. An owner's menu is filled when it opens; one without anything to offer says so.
void fillOwners(QMenu *menu, Engine *e, const Fill &fill);

// The layers as the bin has them: a group, a 3D scene or a merge holding others is a menu of its own. `leaf` adds a layer
// holding nothing; `container` is called with the menu of one that holds others (to put its own entry in, before them).
using LayerLeaf = std::function<void(QMenu *into, quint64 id, const QString &name)>;
using LayerContainer = std::function<void(QMenu *into, quint64 id, const QString &name, bool isGroup)>;
void fillLayerTree(QMenu *menu, Engine *e, const LayerLeaf &leaf, const LayerContainer &container, bool viewports = false);
// The interactions as their list has them (groups, nested)
void fillInteractionTree(QMenu *menu, Engine *e, const LayerLeaf &leaf, const LayerContainer &container);

// A menu emptied (its sub-menus gone too), to be filled again when it opens
void clearMenu(QMenu *menu);
// "Layer › Parameter", "Timeline · Name › Speed", "OSC · …": a value of the show as the operator reads it. `none`: said for no value
QString valueLabel(Engine *e, quint64 layer, const QString &path, const QString &none);

} // namespace showmenu
