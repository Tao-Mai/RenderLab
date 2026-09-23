#pragma once

#include "asset/asset_id.h"

#include <filesystem>

class AssetManager;

class TextureImporter
{
public:
    static AssetId importTexture(
        AssetManager& assets, AssetId id, const std::filesystem::path& source);
    static AssetId importEnvironmentMap(
        AssetManager& assets, AssetId id, const std::filesystem::path& source);
};
