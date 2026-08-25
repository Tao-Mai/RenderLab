#pragma once

#include "base/gfx/mesh.h"

#include <filesystem>
#include <string>
#include <vector>

// 从 assets/ 或 builtin:cube 等 key 加载网格
Mesh LoadMesh(const std::string& key);

std::vector<Mesh> LoadMeshes(const std::filesystem::path& path);
