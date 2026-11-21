#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/pbrt/PbrtSpectrum.h"

struct PbrtMediumAsset;

struct RL_RECORD PbrtSurface
{
    RL_FIELD("alpha") PbrtFloatInput alpha = PbrtFloatConstant{1.0};
    RL_FIELD("reverseOrientation") bool reverseOrientation = false;
    RL_FIELD("insideMedium") std::optional<AssetID<PbrtMediumAsset>> insideMedium;
    RL_FIELD("outsideMedium") std::optional<AssetID<PbrtMediumAsset>> outsideMedium;
    RL_FIELD("faceIndices") std::optional<std::variant<PbrtIntegerValues, PbrtArrayReference>> faceIndices;
    bool operator==(const PbrtSurface&) const = default;
};

#include RL_METADATA_HEADER("metadata/src/asset/pbrt/PbrtSurface.generated.h")
