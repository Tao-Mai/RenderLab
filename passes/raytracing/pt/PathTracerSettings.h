#pragma once

#include "core/MetadataAnnotations.h"

#include "core/Logger.h"
#include "render/RenderConfig.h"

#include <cstdint>

struct RL_RECORD PathTracerSettings
{
    RL_FIELD("maxBounces") uint32_t maxBounces = 2;

    void validate() const { CHECK(maxBounces >= kMinPathBounces && maxBounces <= kMaxPathBounces); }
};

#include RL_METADATA_HEADER("metadata/passes/raytracing/pt/PathTracerSettings.generated.h")
