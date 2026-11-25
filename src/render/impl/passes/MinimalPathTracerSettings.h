#pragma once

#include "core/Logger.h"

#include <cstdint>

struct MinimalPathTracerSettings
{
    static constexpr uint32_t kMaxIndirectBounces = 1u << 16;

    uint32_t maxBounces = 6;

    bool operator==(const MinimalPathTracerSettings&) const = default;

    void validate() const { CHECK(maxBounces <= kMaxIndirectBounces); }
};
