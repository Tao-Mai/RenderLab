#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/TextureBinding.h"
#include "asset/pbrt/PbrtTypes.h"

struct RL_RECORD PbrtImageReference
{
    RL_FIELD("source") PbrtFileReference source;
    RL_FIELD("image") TextureBinding image;
    bool operator==(const PbrtImageReference&) const = default;
};
