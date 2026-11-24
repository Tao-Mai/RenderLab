#pragma once

#include "core/MetadataAnnotations.h"

#include <cstdint>

inline constexpr uint32_t kMaxRestirCandidateCount = 256;
inline constexpr uint32_t kMaxRestirSpatialSamples = 32;
inline constexpr uint32_t kMaxRestirHistoryLength  = 64;
inline constexpr float    kMaxRestirSpatialRadius  = 128.0f;

struct RL_RECORD RestirSettings
{
    RL_FIELD("candidateCount") uint32_t candidateCount     = 32;
    RL_FIELD("temporalReuse") bool     temporalReuse      = true;
    RL_FIELD("spatialReuse") bool     spatialReuse       = true;
    RL_FIELD("maxHistoryLength") uint32_t maxHistoryLength   = 20;
    RL_FIELD("spatialSampleCount") uint32_t spatialSampleCount = 3;
    RL_FIELD("spatialRadius") float    spatialRadius      = 30.0f;

    bool operator==(const RestirSettings&) const = default;
};

struct RL_RECORD RayTracingSettings
{
    RL_FIELD("maxBounces") uint32_t    maxBounces    = 3;
    RL_FIELD("restir") RestirSettings restir;
    RL_FIELD("splitLighting") bool   splitLighting = true;

    void validate() const;
};
