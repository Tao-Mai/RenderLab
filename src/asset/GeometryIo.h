#pragma once

#include "asset/MeshGeometry.h"

#include <filesystem>

namespace geometry_io
{
void write(const std::filesystem::path& file, const MeshGeometry& mesh);
[[nodiscard]] MeshGeometry read(const std::filesystem::path& file);
}
