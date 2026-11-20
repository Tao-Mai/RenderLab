#pragma once

#include <cstdint>

enum class BdptLightSelection
{
    Uniform,
    Power,
};

enum class BdptMisHeuristic
{
    Balance,
    Power,
};

struct BdptSettings
{
    uint32_t           maxBounces     = 6;
    bool               lightTracing   = true;
    BdptLightSelection lightSelection = BdptLightSelection::Power;
    BdptMisHeuristic   misHeuristic   = BdptMisHeuristic::Balance;

    bool operator==(const BdptSettings&) const = default;
    void validate() const;
};
