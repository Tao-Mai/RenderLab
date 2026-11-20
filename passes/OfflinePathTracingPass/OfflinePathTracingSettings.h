#pragma once

#include <cstdint>

enum class OfflineAlgorithm
{
    PT,
    BDPT,
};

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

struct OfflinePathTracingSettings
{
    OfflineAlgorithm algorithm = OfflineAlgorithm::PT;

    struct PathTracing
    {
        uint32_t maxBounces = 2;

        bool operator==(const PathTracing&) const = default;
    } pt;

    struct Bidirectional
    {
        uint32_t           maxBounces     = 6;
        bool               lightTracing   = true;
        BdptLightSelection lightSelection = BdptLightSelection::Power;
        BdptMisHeuristic   misHeuristic   = BdptMisHeuristic::Balance;

        bool operator==(const Bidirectional&) const = default;
    } bdpt;

    [[nodiscard]] uint32_t maxBounces() const
    {
        return algorithm == OfflineAlgorithm::PT ? pt.maxBounces : bdpt.maxBounces;
    }

    bool operator==(const OfflinePathTracingSettings&) const = default;

    void validate() const;
    [[nodiscard]] bool requiresGraphRebuild(const OfflinePathTracingSettings& current) const
    {
        return algorithm != current.algorithm;
    }
};
