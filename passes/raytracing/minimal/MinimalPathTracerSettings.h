#pragma once

#include "core/MetadataAnnotations.h"

#include "core/Logger.h"
#include "render/RenderConfig.h"

#include <cstdint>

struct RL_RECORD MinimalPathTracerSettings
{
    RL_FIELD("maxBounces") uint32_t maxBounces = 3;

    void validate() const { CHECK(maxBounces >= kMinPathBounces && maxBounces <= kMaxPathBounces); }
};

#include RL_METADATA_HEADER("metadata/passes/raytracing/minimal/MinimalPathTracerSettings.generated.h")
