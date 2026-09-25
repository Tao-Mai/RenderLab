#pragma once

#include "asset/AssetId.h"
#include "asset/ImageFormat.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <variant>
#include <vector>

struct TextureDesc;

class AssetImporter
{
public:
    static AssetId importTexture(const std::filesystem::path& source);

    static AssetId importEnvironmentMap(const std::filesystem::path& source);

    static AssetId importMesh(const std::filesystem::path& source);

private:
    struct LoadedImage
    {
        enum class Projection
        {
            LatLong,
            Cube,
        };

        ImageFormat                                            format;
        uint32_t                                               width  = 0;
        uint32_t                                               height = 0;
        std::optional<Projection>                              projection;
        std::variant<std::vector<uint8_t>, std::vector<float>> pixels;
    };

    [[nodiscard]] static LoadedImage loadImage(const std::filesystem::path& source);
    [[nodiscard]] static AssetId     saveTexture(
        TextureDesc                  desc,
        const std::filesystem::path& source,
        std::span<const uint8_t>     bytes);
};
