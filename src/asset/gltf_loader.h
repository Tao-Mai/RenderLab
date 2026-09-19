#pragma once

#include "asset/mesh_data.h"

#include <filesystem>

class GLTFLoader
{
  public:
    [[nodiscard]] static MeshData load(const std::filesystem::path &path);
};
