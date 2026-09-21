#pragma once

#include "asset/mesh_data.h"

namespace BuiltinMeshes
{
    [[nodiscard]] MeshData cube();
    [[nodiscard]] MeshData sphere(uint32_t segments = 32, uint32_t rings = 16);
    [[nodiscard]] MeshData arrow();
}
