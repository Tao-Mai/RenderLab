#pragma once

#include "asset/Asset.h"
#include "asset/TextureBinding.h"

struct EnvironmentMapAsset : Asset
{
    using ID                              = AssetID<EnvironmentMapAsset>;
    static constexpr std::string_view dir = "EnvironmentMap";

    ID             id;
    TextureBinding radiance;
    TextureBinding irradiance;
    TextureBinding prefilteredSpecular;
};
