#pragma once

#include "core/MetadataAnnotations.h"

#include "core/Logger.h"

#include <cstdint>

struct RL_RECORD MinimalPathTracerSettings
{
    static constexpr uint32_t kMaxIndirectBounces = 1u << 16;

    RL_FIELD("maxBounces") uint32_t maxBounces = 6;

    void validate() const { CHECK(maxBounces <= kMaxIndirectBounces); }
};
