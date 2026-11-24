#pragma once

#include "core/MetadataAnnotations.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include "asset/Asset.h"
#include "asset/ImageFormat.h"

struct RL_RECORD TextureAsset : Asset
{
    using ID                              = AssetID<TextureAsset>;
    static constexpr std::string_view dir = "Texture";

    RL_FIELD("id") ID                                   id;
    RL_FIELD("source") Source                               source = Source::File;
    RL_FIELD("format") ImageFormat                          format;
    RL_FIELD("colorSpace") ColorSpace                           colorSpace;
    RL_FIELD("chromaticities") std::optional<RgbChromaticities>      chromaticities;
    RL_FIELD("layout") ImageLayout                          layout;
    RL_FIELD("width") uint32_t                             width{};
    RL_FIELD("height") uint32_t                             height{};
    RL_FIELD("mipLevels") uint32_t                             mipLevels{};
    RL_FIELD("path") std::optional<std::filesystem::path> path;
    RL_FIELD("binary") std::optional<std::filesystem::path> binary;
};
