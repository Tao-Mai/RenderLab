#pragma once

#include "asset/imported_mesh.h"

#include <filesystem>

class GLTFLoader
{
  public:
    [[nodiscard]] static ImportedMesh load(const std::filesystem::path &path);
};
