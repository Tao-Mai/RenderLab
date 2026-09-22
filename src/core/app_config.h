#pragma once

#include "asset/asset_id.h"

#include <filesystem>
#include <string>

struct AppPaths
{
    std::filesystem::path assets;
};

struct AppConfig
{
    AppPaths paths;
    AssetId initialScene;
};
