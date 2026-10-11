#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include "asset/Asset.h"
#include "asset/ImageFormat.h"

struct TextureAsset : Asset
{
    using ID                              = AssetID<TextureAsset>;
    static constexpr std::string_view dir = "Texture";

    ID                                   id;
    Source                               source = Source::File;
    ImageFormat                          format;
    ColorSpace                           colorSpace;
    ImageLayout                          layout;
    uint32_t                             width{};
    uint32_t                             height{};
    uint32_t                             mipLevels{};
    std::optional<std::filesystem::path> path;
    std::optional<std::filesystem::path> binary;
};
