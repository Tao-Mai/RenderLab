#pragma once

#include "asset/pbrt/PbrtSpectrum.h"

struct PbrtMediumAsset;

struct PbrtSurface
{
    PbrtFloatInput alpha = PbrtFloatConstant{1.0};
    bool reverseOrientation = false;
    std::optional<AssetID<PbrtMediumAsset>> insideMedium;
    std::optional<AssetID<PbrtMediumAsset>> outsideMedium;
    std::optional<std::variant<PbrtIntegerValues, PbrtArrayReference>> faceIndices;
    bool operator==(const PbrtSurface&) const = default;
};
