#pragma once

#include "asset/asset_desc.h"

#include <filesystem>

class AssetManager;

class GLTFLoader
{
public:
    static void import(
        AssetManager& assets,
        AssetId meshId,
        const std::filesystem::path& path,
        MeshSourceDesc source);
};
