#pragma once

#include "asset/Asset.h"
struct TextureAsset;
struct SamplerAsset;

struct TextureBinding
{
    AssetID<TextureAsset> textureID;
    AssetID<SamplerAsset> samplerID;

    bool operator==(const TextureBinding&) const = default;
};
