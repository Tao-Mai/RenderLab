#pragma once

#include "asset/imported_mesh.h"

namespace BuiltinMeshes
{
    [[nodiscard]] ImportedMesh cube();
    [[nodiscard]] ImportedMesh sphere(uint32_t segments = 32, uint32_t rings = 16);
    [[nodiscard]] ImportedMesh arrow();
}
