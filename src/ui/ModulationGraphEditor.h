#pragma once
#include "ModulationGraph.h"
#include <functional>
class Engine;
class QWidget;
// Edits are applied live through the interaction panel's undo stack.
void editModulationGraph(Engine *engine, quint64 interaction, const ModulationGraph &graph,
                         const std::function<void(const ModulationGraph &)> &commit, QWidget *parent);
