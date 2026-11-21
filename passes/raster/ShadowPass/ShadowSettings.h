#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/SamplerAsset.h"

#include <cstdint>

enum class RL_ENUM CascadeSplitStrategy
{
    Practical RL_ENUM_VALUE("Practical"),
    Uniform RL_ENUM_VALUE("Uniform"),
};

struct RL_RECORD ShadowSettings
{
    struct RL_RECORD Cascade
    {
        RL_FIELD("count") uint32_t             count         = 4;
        RL_FIELD("shadowMapSize") uint32_t             shadowMapSize = 2048;
        RL_FIELD("maxDistance") float                maxDistance   = 50.0f;
        RL_FIELD("splitStrategy") CascadeSplitStrategy splitStrategy = CascadeSplitStrategy::Practical;
        RL_FIELD("splitLambda") float                splitLambda   = 0.5f;
        RL_FIELD("blendRatio") float                blendRatio    = 0.1f;  // Fraction of each slice's depth interval; 0 disables blending.
        RL_FIELD("sampleRadius") float                sampleRadius  = 2.0f;  // PCF radius in shadow-map texels.
    } cascade RL_FIELD("cascade");
    RL_FIELD("pointShadowMapSize") uint32_t             pointShadowMapSize    = 1024;
    RL_FIELD("rectAreaShadowMapSize") uint32_t             rectAreaShadowMapSize = 1024;
    RL_FIELD("spotShadowMapSize") uint32_t             spotShadowMapSize     = 1024;
    RL_FIELD("bias") float                bias                  = 0.001f;
    RL_FIELD("filter") SamplerAsset::Filter filter                = SamplerAsset::Filter::Linear;
    RL_FIELD("pixelRotation") bool                 pixelRotation         = true;
    RL_FIELD("sampleRadius") float                sampleRadius          = 0.02f;
    RL_FIELD("spotSampleRadius") float                spotSampleRadius      = 1.5f;  // Texels, independent of shadow map resolution.

    void validate() const;
    [[nodiscard]] bool requiresGraphRebuild(const ShadowSettings& current) const;
};

#include RL_METADATA_HEADER("metadata/passes/raster/ShadowPass/ShadowSettings.generated.h")
