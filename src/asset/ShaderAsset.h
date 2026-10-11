#pragma once

#include <filesystem>
#include "asset/Asset.h"

struct ShaderAsset : Asset
{
    using ID                              = AssetID<ShaderAsset>;
    static constexpr std::string_view dir = "Shader";

    ID                    id;
    std::filesystem::path binary;
};
