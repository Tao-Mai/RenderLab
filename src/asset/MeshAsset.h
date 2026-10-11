#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>
#include "asset/Asset.h"
struct MaterialAsset;

struct MeshAsset : Asset
{
    using ID                              = AssetID<MeshAsset>;
    static constexpr std::string_view dir = "Mesh";

    struct Submesh
    {
        uint32_t               firstIndex = 0;
        uint32_t               indexCount = 0;
        AssetID<MaterialAsset> materialId;
    };

    ID                                   id;
    Source                               source;
    std::optional<std::filesystem::path> path;
    float                                importScale = 1.0f;
    std::filesystem::path                geometry;
    std::vector<Submesh>                 submeshes;
};
