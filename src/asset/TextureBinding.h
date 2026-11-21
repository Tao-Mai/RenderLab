#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/Asset.h"
struct TextureAsset;
struct SamplerAsset;

struct RL_RECORD TextureBinding
{
    RL_FIELD("textureID") AssetID<TextureAsset> textureID;
    RL_FIELD("samplerID") AssetID<SamplerAsset> samplerID;

    bool operator==(const TextureBinding&) const = default;
};

#include RL_METADATA_HEADER("metadata/src/asset/TextureBinding.generated.h")
