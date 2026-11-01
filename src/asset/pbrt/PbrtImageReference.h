#pragma once

#include "asset/TextureBinding.h"
#include "asset/pbrt/PbrtTypes.h"

struct PbrtImageReference
{
    PbrtFileReference source;
    TextureBinding image;
    bool operator==(const PbrtImageReference&) const = default;
};
