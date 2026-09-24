#pragma once

#include "asset/MeshGeometry.h"

namespace BuiltinMeshes
{
[[nodiscard]] MeshGeometry cube();
[[nodiscard]] MeshGeometry sphere(uint32_t segments = 32, uint32_t rings = 16);
[[nodiscard]] MeshGeometry plane();
[[nodiscard]] MeshGeometry arrow();
}
