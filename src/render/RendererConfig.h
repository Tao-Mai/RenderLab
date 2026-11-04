#pragma once

#include "asset/SamplerAsset.h"
#include "render/ExposureConfig.h"

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

// Maximum surface scattering events; terminal emission is evaluated after the last bounce.
inline constexpr uint32_t kMinPathBounces       = 1;
inline constexpr uint32_t kMaxPathBounces       = 64;
inline constexpr uint32_t kMaxBloomLevels       = 6;
inline constexpr uint32_t kMaxSvgfHistoryLength    = 1024;
inline constexpr uint32_t kMaxRestirCandidateCount = 256;
inline constexpr uint32_t kMaxRestirSpatialSamples = 32;
inline constexpr uint32_t kMaxRestirHistoryLength  = 64;
inline constexpr float    kMaxRestirSpatialRadius  = 128.0f;

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
        ExposureConfig      exposure;
        ToneMappingOperator mode                 = ToneMappingOperator::Neutral;
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
        uint32_t maxBounces    = 3;
        bool     svgf          = true;
        uint32_t historyLength = 32;    // Arithmetic averaging through this sample count.
        float    emaAlpha      = 0.1f;  // Current-sample weight after historyLength samples.
    } rayTracing;

    struct Restir
    {
        uint32_t candidateCount     = 32;
        bool     temporalReuse      = true;
        bool     spatialReuse       = true;
        uint32_t maxHistoryLength   = 20;
        uint32_t spatialSampleCount = 3;
        float    spatialRadius      = 30.0f;

        bool operator==(const Restir&) const = default;
    } restir;

    struct Offline
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

        bool operator==(const Offline&) const = default;
    } offline;
};

struct RendererConfig
{
    RenderPath    renderPath = RenderPath::Forward;
    RenderFeature feature;
};
