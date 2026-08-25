#pragma once

#include <filesystem>
#include <vector>

#include "base/gfx/mesh.h"

std::vector<Mesh> LoadMeshes(const std::filesystem::path& path);
