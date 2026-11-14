#pragma once
// Tests of the 3D FX: a parameter of the first 3D FX of a kind of a layer ("fx3d/<id>/amount"), the FX added (on) when the
// layer has none
#include "Engine.h"

inline QString fxPath(Engine &engine, quint64 layer, Fx3d kind, const char *name)
{
    const int index = engine.indexOfId(layer);
    quint64 id = engine.fx3dOfKind(index, kind);
    if (!id) id = engine.addFx3d(index, kind);
    return QStringLiteral("fx3d/%1/%2").arg(id).arg(QLatin1String(name));
}
