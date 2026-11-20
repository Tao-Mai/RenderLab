#pragma once

#include "asset/SamplerAsset.h"

#include <cstdint>

enum class CascadeSplitStrategy
{
    Practical,
    Uniform,
};

struct ShadowSettings
{
    struct Cascade
    {
        uint32_t             count         = 4;
        uint32_t             shadowMapSize = 2048;
        float                maxDistance   = 50.0f;
        CascadeSplitStrategy splitStrategy = CascadeSplitStrategy::Practical;
        float                splitLambda   = 0.5f;
        float                blendRatio    = 0.1f;  // Fraction of each slice's depth interval; 0 disables blending.
        float                sampleRadius  = 2.0f;  // PCF radius in shadow-map texels.
    } cascade;
    uint32_t             pointShadowMapSize    = 1024;
    uint32_t             rectAreaShadowMapSize = 1024;
    uint32_t             spotShadowMapSize     = 1024;
    float                bias                  = 0.001f;
    SamplerAsset::Filter filter                = SamplerAsset::Filter::Linear;
    bool                 pixelRotation         = true;
    float                sampleRadius          = 0.02f;
    float                spotSampleRadius      = 1.5f;  // Texels, independent of shadow map resolution.

    void validate() const;
    [[nodiscard]] bool requiresGraphRebuild(const ShadowSettings& current) const;
};
