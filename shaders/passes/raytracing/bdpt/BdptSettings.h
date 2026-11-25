#pragma once

#include "core/MetadataAnnotations.h"

#include <cstdint>

enum class RL_ENUM BdptLightSelection
{
    Uniform RL_ENUM_VALUE("Uniform"),
    Power RL_ENUM_VALUE("Power"),
};

enum class RL_ENUM BdptMisHeuristic
{
    Balance RL_ENUM_VALUE("Balance"),
    Power RL_ENUM_VALUE("Power"),
};

struct RL_RECORD BdptSettings
{
    RL_FIELD("maxBounces") uint32_t           maxBounces     = 6;
    RL_FIELD("lightTracing") bool               lightTracing   = true;
    RL_FIELD("lightSelection") BdptLightSelection lightSelection = BdptLightSelection::Power;
    RL_FIELD("misHeuristic") BdptMisHeuristic   misHeuristic   = BdptMisHeuristic::Balance;

    bool operator==(const BdptSettings&) const = default;
    void validate() const;
};
