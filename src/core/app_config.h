#pragma once

#include "asset/asset_id.h"

#include <filesystem>
#include <string>

struct AppPaths
{
    std::filesystem::path assets;
    std::filesystem::path meshDescs;
    std::filesystem::path materialDescs;
    std::filesystem::path textureDescs;
    std::filesystem::path shaderDescs;
    std::filesystem::path sceneDescs;
    std::filesystem::path geometry;
};

struct AppConfig
{
    AppPaths paths;
    AssetId initialScene;
};
