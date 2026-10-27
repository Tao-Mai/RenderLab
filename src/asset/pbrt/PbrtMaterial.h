#pragma once

#include "asset/MaterialAsset.h"
#include "asset/pbrt/PbrtTypes.h"

struct PbrtMaterial : MaterialAsset
{
    std::string name;
    PbrtEntity  entity;

    bool operator==(const PbrtMaterial&) const = default;
};
