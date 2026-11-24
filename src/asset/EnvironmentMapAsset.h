#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/Asset.h"
#include "asset/TextureBinding.h"

struct RL_RECORD EnvironmentMapAsset : Asset
{
    using ID                              = AssetID<EnvironmentMapAsset>;
    static constexpr std::string_view dir = "EnvironmentMap";

    RL_FIELD("id") ID             id;
    RL_FIELD("radiance") TextureBinding radiance;
    RL_FIELD("irradiance") TextureBinding irradiance;
    RL_FIELD("prefilteredSpecular") TextureBinding prefilteredSpecular;
};
