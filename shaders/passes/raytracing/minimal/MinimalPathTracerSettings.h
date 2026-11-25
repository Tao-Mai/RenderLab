#pragma once

#include "core/MetadataAnnotations.h"

#include "core/Logger.h"

#include <cstdint>

struct RL_RECORD MinimalPathTracerSettings
{
    static constexpr uint32_t kMaxIndirectBounces = 1u << 16;

    RL_PASS_SLIDER("maxBounces", 0, kMaxIndirectBounces)
    RL_PASS_UI("Max bounces", "Maximum indirect bounces. 0 gives direct illumination only.")
    uint32_t maxBounces = 6;

    void validate() const { CHECK(maxBounces <= kMaxIndirectBounces); }
};
