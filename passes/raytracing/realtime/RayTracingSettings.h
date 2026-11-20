#pragma once

#include <cstdint>

inline constexpr uint32_t kMaxRestirCandidateCount = 256;
inline constexpr uint32_t kMaxRestirSpatialSamples = 32;
inline constexpr uint32_t kMaxRestirHistoryLength  = 64;
inline constexpr float    kMaxRestirSpatialRadius  = 128.0f;

struct RestirSettings
{
    uint32_t candidateCount     = 32;
    bool     temporalReuse      = true;
    bool     spatialReuse       = true;
    uint32_t maxHistoryLength   = 20;
    uint32_t spatialSampleCount = 3;
    float    spatialRadius      = 30.0f;

    bool operator==(const RestirSettings&) const = default;
};

struct RayTracingSettings
{
    uint32_t          maxBounces    = 3;
    RestirSettings    restir;

    void validate() const;
};
