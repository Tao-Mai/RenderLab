#pragma once

#include "asset/AssetId.h"
#include "asset/MeshGeometry.h"

#include <cstdint>
#include <filesystem>
#include <vector>

class BuiltinAssetManager
{
public:
    static inline const AssetId cube   = "cube";
    static inline const AssetId sphere = "sphere";
    static inline const AssetId plane  = "plane";
    static inline const AssetId arrow  = "arrow";

    void init();

    [[nodiscard]] MeshGeometry meshGeometry(
        const std::filesystem::path& geometry) const;
    [[nodiscard]] std::vector<uint8_t> textureData(const AssetId& id) const;
};
