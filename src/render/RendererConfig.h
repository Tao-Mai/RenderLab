#pragma once

#include "asset/Asset.h"

enum class RenderPath
{
    Rasterization,
    RayTracing
};

enum class ToneMappingOperator
{
    Neutral,
    Reinhard,
};

struct RenderFeature
{
    bool  ibl                = true;
    float ambientCoefficient = 0.1f;
    bool  hdr                = true;
    struct Shadow
    {
        bool enable = true;
        struct Cascade
        {
            uint32_t count         = 4;
            uint32_t shadowMapSize = 2048;
            float    maxDistance   = 50.0f;
            float    splitLambda   = 0.5f;
            float    depthPadding  = 10.0f;
        } cascade;
        uint32_t             pointShadowMapSize    = 1024;
        uint32_t             rectAreaShadowMapSize = 1024;
        float                bias                  = 0.001f;
        SamplerAsset::Filter filter                = SamplerAsset::Filter::Linear;
        bool                 pixelRotation         = true;
        float                sampleRadius          = 0.02f;
    } shadow;

    struct ToneMapping
    {
        float               exposure = 0.0f;  // Stops: +1 doubles linear scene brightness.
        ToneMappingOperator mode     = ToneMappingOperator::Neutral;
    } toneMapping;
};

struct RendererConfig
{
    RenderPath                             renderPath = RenderPath::Rasterization;
    TextureBinding                         brdfLut;
    RenderFeature                          feature;
    std::optional<EnvironmentMapAsset::ID> environmentMap;
};
