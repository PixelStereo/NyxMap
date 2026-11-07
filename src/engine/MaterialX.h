#pragma once

#include "Model3D.h"

// Import a texture-backed Standard Surface into NyxMap's real-time material.
bool importMaterialX(const QString &path, Material3D &material, QString *error);
