#pragma once

#include "asset/Asset.h"
#include "asset/pbrt/PbrtTypes.h"

enum class PbrtTextureType
{
    Float,
    Spectrum
};
struct PbrtTextureAsset : Asset
{
    using ID                              = AssetID<PbrtTextureAsset>;
    static constexpr std::string_view dir = "PbrtTexture";

    ID              id;
    std::string     name;
    PbrtTextureType valueType = PbrtTextureType::Float;
    PbrtEntity      entity;
    PbrtTransform   renderFromTexture;

    bool operator==(const PbrtTextureAsset&) const = default;
};
