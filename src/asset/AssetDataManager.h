#pragma once

#include "asset/Asset.h"
#include "asset/MeshGeometry.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

class AssetDataManager
{
public:
    void init();

    [[nodiscard]] std::filesystem::path writeGeometry(
        const MeshAsset::ID& id, const MeshGeometry& mesh) const;
    [[nodiscard]] MeshGeometry readGeometry(const MeshAsset& asset) const;

    [[nodiscard]] std::filesystem::path writeTexture(
        const TextureAsset& asset, std::span<const uint8_t> bytes) const;
    [[nodiscard]] std::vector<uint8_t> readTexture(const TextureAsset& asset) const;

    [[nodiscard]] static uint32_t bytesPerPixel(ImageFormat format);

private:
    std::filesystem::path assetsRoot;

    [[nodiscard]] std::filesystem::path resolve(const std::filesystem::path& relative) const;
};
