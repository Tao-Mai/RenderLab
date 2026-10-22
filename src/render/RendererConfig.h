#pragma once

#include <cmath>

#include "asset/SamplerAsset.h"

enum class RenderPath
{
    Forward,
    Deferred,
    RayTracing,
    OfflinePathTracing
};

enum class ToneMappingOperator
{
    Neutral,
    Reinhard,
};

enum class CascadeSplitStrategy
{
    Practical,
    Uniform,
};

// The primary ray and its visibility query each consume one recursion level.
inline constexpr uint32_t kMinRayRecursionDepth   = 2;
inline constexpr float    kExposureCalibration    = 1.2f;
inline constexpr uint32_t kMaxBloomLevels         = 6;
inline constexpr uint32_t kMaxEmaStartSampleCount = 1024;

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
    } shadow;

    struct ToneMapping
    {
        float               ev100                = 0.0f;
        float               exposureCompensation = 0.0f;  // Stops: +1 doubles brightness.
        ToneMappingOperator mode                 = ToneMappingOperator::Neutral;

        [[nodiscard]] float exposureScale() const
        {
            return std::exp2(exposureCompensation - ev100) / kExposureCalibration;
        }
    } toneMapping;

    struct Bloom
    {
        bool     enable       = true;
        float    intensity    = 0.04f;
        float    scatter      = 0.7f;  // Low-resolution contribution when reconstructing the pyramid.
        float    filterRadius = 1.0f;  // Scale of the Gaussian sampling offsets.
        uint32_t levels       = 5;
    } bloom;

    struct RayTracing
    {
        uint32_t maxRecursionDepth   = 4;
        bool     svgf                = true;
        uint32_t emaStartSampleCount = 10;
        float    emaAlpha            = 0.1f;  // Weight of the current sample after the arithmetic-mean warmup.
    } rayTracing;

    struct Offline
    {
        uint32_t maxRecursionDepth = 6;
    } offline;
};

struct RendererConfig
{
    RenderPath    renderPath = RenderPath::Forward;
    RenderFeature feature;
};
