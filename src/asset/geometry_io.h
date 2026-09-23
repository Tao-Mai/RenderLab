#pragma once

#include "asset/mesh_geometry.h"

#include <filesystem>

namespace geometry_io
{
void write(const std::filesystem::path& file, const MeshGeometry& mesh);
[[nodiscard]] MeshGeometry read(const std::filesystem::path& file);
}
