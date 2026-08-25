#pragma once

#include <cstdint>
#include <string_view>

enum class AssetType : uint8_t
{
    Mesh  = 0,
    Scene = 1,
};

[[nodiscard]] constexpr std::string_view asset_type_folder(AssetType type)
{
    switch (type)
    {
    case AssetType::Mesh:
        return "mesh";
    case AssetType::Scene:
        return "scene";
    }
    return "unknown";
}
