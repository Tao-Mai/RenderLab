#pragma once

#include <cmath>

#include "asset/SamplerAsset.h"
#include "asset/TextureBinding.h"

enum class RenderPath
{
    Rasterization,
    Deferred,
    RayTracing,
    OfflinePathTracing
};

enum class ToneMappingOperator
{
    Neutral,
    Reinhard,
};

// The primary ray and its visibility query each consume one recursion level.
inline constexpr uint32_t kMinRayRecursionDepth = 2;
inline constexpr float    kExposureCalibration  = 1.2f;
inline constexpr uint32_t kMaxBloomLevels        = 6;

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
        uint32_t             spotShadowMapSize     = 1024;
        float                bias                  = 0.001f;
        SamplerAsset::Filter filter                = SamplerAsset::Filter::Linear;
        bool                 pixelRotation         = true;
        float                sampleRadius          = 0.02f;
        float                spotSampleRadius      = 1.5f; // Texels, independent of shadow map resolution.
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
        float    filterRadius = 1.0f; // Texels of each downsampled level.
        uint32_t levels       = 5;
    } bloom;

    struct RayTracing
    {
        uint32_t maxRecursionDepth = 4;
    } rayTracing;

    struct Offline
    {
        uint32_t maxRecursionDepth = 6;
    } offline;
};

struct RendererConfig
{
    RenderPath     renderPath = RenderPath::Rasterization;
    TextureBinding brdfLut;
    RenderFeature  feature;
};
