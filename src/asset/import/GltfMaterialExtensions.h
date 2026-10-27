#pragma once

#include "asset/GltfMaterial.h"
#include "asset/TextureAsset.h"

#include <filesystem>
#include <functional>

namespace tinygltf
{
struct Material;
}

using GltfTextureLoader = std::function<std::optional<TextureBinding>(int, ColorSpace)>;

void importMaterialExtensions(
    GltfMaterial&            material,
    const tinygltf::Material&    source,
    const std::filesystem::path& sourcePath,
    const GltfTextureLoader&     loadTexture
);
