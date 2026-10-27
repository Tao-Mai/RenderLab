#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "render/RenderConfig.h"

namespace RenderInterface
{
inline constexpr uint32_t kSceneSet    = 0;
inline constexpr uint32_t kLightingSet = 1;
inline constexpr uint32_t kMaterialSet = 2;
inline constexpr uint32_t kPassSet     = 3;

inline constexpr uint32_t kGBufferAlbedoBinding   = 0;
inline constexpr uint32_t kGBufferNormalBinding   = 1;
inline constexpr uint32_t kGBufferEmissiveBinding = 2;
inline constexpr uint32_t kGBufferDepthBinding    = 3;
inline constexpr uint32_t kGBufferSheenBinding    = 4;

// Scene set: camera data only.
inline constexpr uint32_t kSceneUniformBinding = 0;

// Lighting data: UBO, then SSBOs.
inline constexpr uint32_t kLightUniformBinding            = 0;
inline constexpr uint32_t kLightBufferBinding             = 1;
inline constexpr uint32_t kPointShadowBufferBinding       = 2;
inline constexpr uint32_t kSpotShadowBufferBinding        = 3;
inline constexpr uint32_t kRectAreaShadowBufferBinding    = 4;
inline constexpr uint32_t kDirectionalShadowBufferBinding = 5;

// Shadow images, then comparison and regular samplers.
inline constexpr uint32_t kPointShadowImageBinding        = 6;
inline constexpr uint32_t kSpotShadowImageBinding         = 7;
inline constexpr uint32_t kRectAreaShadowImageBinding     = 8;
inline constexpr uint32_t kDirectionalShadowImageBinding  = 9;
inline constexpr uint32_t kShadowComparisonSamplerBinding = 10;
inline constexpr uint32_t kLinearSamplerBinding           = 11;

// Environment textures: adjacent image / sampler pairs.
inline constexpr uint32_t kIrradianceImageBinding            = 12;
inline constexpr uint32_t kIrradianceSamplerBinding          = 13;
inline constexpr uint32_t kPrefilteredSpecularImageBinding   = 14;
inline constexpr uint32_t kPrefilteredSpecularSamplerBinding = 15;
inline constexpr uint32_t kBrdfLutImageBinding               = 16;
inline constexpr uint32_t kBrdfLutSamplerBinding             = 17;
inline constexpr uint32_t kRadianceImageBinding              = 18;
inline constexpr uint32_t kRadianceSamplerBinding            = 19;
inline constexpr uint32_t kSheenLutImageBinding              = 20;
inline constexpr uint32_t kSheenLutSamplerBinding            = 21;

// Global material set, shared by rasterization and ray tracing.
inline constexpr uint32_t kMaterialBufferBinding   = 0;
inline constexpr uint32_t kMaterialTexturesBinding = 1;
inline constexpr uint32_t kMaterialSamplersBinding = 2;

// Tone mapping uses its own set 0, not the scene layout.
inline constexpr uint32_t kToneMappingImageBinding   = 0;
inline constexpr uint32_t kToneMappingSamplerBinding = 1;
}  // namespace RenderInterface

// Matches the std140 SceneUniforms block in shaders/scene_data.slang.
struct alignas(16) SceneUniforms
{
    glm::mat4 viewProjection{1.0f};
    glm::mat4 inverseViewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
    glm::mat4 view{1.0f};
};

// Exclusive group ends in one SSBO: Directional, Point, RectArea, Spot.
// Only enabled lights are uploaded; an empty group has equal begin/end indices.
struct LightRanges
{
    bool     operator==(const LightRanges&) const = default;
    uint32_t directionalEnd                       = 0;
    uint32_t pointEnd                             = 0;
    uint32_t rectAreaEnd                          = 0;
    uint32_t spotEnd                              = 0;
};
static_assert(sizeof(LightRanges) == 16);

// The SSBO contains lightRanges.spotEnd active entries. IBL.x is
// the maximum LOD of the scene's prefiltered specular cubemap. shadowParameters.x
// is the normalized shadow depth bias, y is the point-shadow direction-space PCF radius,
// z enables per-pixel rotation (0/1), and w is the spot-shadow PCF radius in texels.
struct alignas(16) LightUniforms
{
    LightRanges lightRanges;
    float       ambientCoefficient            = 0.1f;
    uint32_t    cascadeCount                  = 0;
    float       directionalShadowSampleRadius = 0.0f;  // Shadow-map texels.
    float       padding                       = 0.0f;
    glm::vec4   iblParameters{0.0f};
    glm::vec4   shadowParameters{0.0f};
};

// GPU discriminator; values match shaders/light_data.slang.
enum class GpuLightType : uint32_t
{
    Point       = 0,
    Directional = 1,
    RectArea    = 2,
    Spot        = 3,
};

// One std430 SSBO element, with a 96-byte array stride. CPU preparation removes disabled lights.
struct alignas(16) LightData
{
    bool operator==(const LightData&) const = default;
    // Photometric-scaled RGB: Point/Spot intensity, Directional irradiance, RectArea radiance.
    glm::vec3    color{0.0f};
    float        padding3 = 0.0f;
    glm::vec4    positionRange{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4    direction{0.0f, -1.0f, 0.0f, 0.0f};
    glm::vec4    areaSizeCone{1.0f, 1.0f, 0.9396926f, 0.8660254f};
    GpuLightType lightType     = GpuLightType::Point;
    uint32_t     shadowEnabled = 0;
    uint32_t     shadowIdx     = ~0u;
    uint32_t     padding0      = 0;
    glm::vec4    areaRightData{1.0f, 0.0f, 0.0f, 0.0f};

    [[nodiscard]] glm::vec3 areaRight() const { return xyz(areaRightData); }

    [[nodiscard]] glm::vec3 position() const { return xyz(positionRange); }

    [[nodiscard]] float range() const { return positionRange.w; }

    [[nodiscard]] GpuLightType type() const { return lightType; }

    [[nodiscard]] bool castShadow() const { return shadowEnabled != 0; }
};

struct alignas(16) PointShadowData
{
    bool                     operator==(const PointShadowData&) const = default;
    std::array<glm::mat4, 6> vp;
};

struct alignas(16) SpotShadowData
{
    bool      operator==(const SpotShadowData&) const = default;
    glm::mat4 vp{1.0f};
};

struct alignas(16) RectAreaShadowData
{
    bool      operator==(const RectAreaShadowData&) const = default;
    glm::mat4 vp{1.0f};
    float     nearPlane = 0.0f;
    float     farPlane  = 0.0f;
    // World-space radius at light-view depth z maps to radius * worldToUvScale / z.
    glm::vec2 worldToUvScale{0.0f};
};

struct alignas(16) CascadeShadowData
{
    bool      operator==(const CascadeShadowData&) const = default;
    glm::mat4 vp{1.0f};
    float     depthUpperBound = 0.0f;  // Positive camera-view depth, not distance to the camera.
    float     blendStartDepth = 0.0f;
    float     padding1        = 0.0f;
    float     padding2        = 0.0f;
};

struct alignas(16) DirectionalShadowData
{
    bool operator==(const DirectionalShadowData&) const = default;
    std::array<CascadeShadowData, kMaxDirectionalShadowCascades> cascades;
};

static_assert(sizeof(SceneUniforms) == 208);
static_assert(offsetof(SceneUniforms, inverseViewProjection) == 64);
static_assert(offsetof(SceneUniforms, cameraPosition) == 128);
static_assert(offsetof(SceneUniforms, view) == 144);
static_assert(sizeof(LightUniforms) == 64);
static_assert(offsetof(LightUniforms, lightRanges) == 0);
static_assert(offsetof(LightUniforms, directionalShadowSampleRadius) == 24);
static_assert(offsetof(LightUniforms, ambientCoefficient) == 16);
static_assert(offsetof(LightUniforms, cascadeCount) == 20);
static_assert(offsetof(LightUniforms, padding) == 28);
static_assert(offsetof(LightUniforms, iblParameters) == 32);
static_assert(offsetof(LightUniforms, shadowParameters) == 48);
static_assert(sizeof(LightData) == 96);
static_assert(offsetof(LightData, color) == 0);
static_assert(offsetof(LightData, padding3) == 12);
static_assert(offsetof(LightData, positionRange) == 16);
static_assert(offsetof(LightData, direction) == 32);
static_assert(offsetof(LightData, areaSizeCone) == 48);
static_assert(offsetof(LightData, lightType) == 64);
static_assert(offsetof(LightData, shadowEnabled) == 68);
static_assert(offsetof(LightData, shadowIdx) == 72);
static_assert(offsetof(LightData, areaRightData) == 80);
static_assert(sizeof(PointShadowData) == 384);
static_assert(offsetof(PointShadowData, vp) == 0);
static_assert(sizeof(RectAreaShadowData) == 80);
static_assert(sizeof(SpotShadowData) == 64);
static_assert(offsetof(RectAreaShadowData, vp) == 0);
static_assert(offsetof(RectAreaShadowData, nearPlane) == 64);
static_assert(offsetof(RectAreaShadowData, farPlane) == 68);
static_assert(offsetof(RectAreaShadowData, worldToUvScale) == 72);
static_assert(sizeof(CascadeShadowData) == 80);
static_assert(offsetof(CascadeShadowData, depthUpperBound) == 64);
static_assert(offsetof(CascadeShadowData, blendStartDepth) == 68);
static_assert(sizeof(DirectionalShadowData) == kMaxDirectionalShadowCascades * sizeof(CascadeShadowData));

struct MeshPushConstants
{
    glm::mat4 model{1.0f};
    uint32_t  materialIndex = 0;
    uint32_t  padding[3]{};
};

struct ShadowPushConstants
{
    glm::mat4 model{1.0f};
    uint32_t  lightIdx     = 0;
    uint32_t  cascadeIndex = 0;
    uint32_t  padding1     = 0;
    uint32_t  padding2     = 0;
};

struct SkyboxPushConstants
{
    glm::vec4 viewport{0.0f};
};

struct DeferredLightingPushConstants
{
    glm::vec4 viewport{0.0f};
};

struct ToneMappingPushConstants
{
    float    exposureScale = 1.0f;
    uint32_t mode          = 0;
    uint32_t encodeSrgb    = 0;
    uint32_t padding       = 0;
};

static_assert(sizeof(MeshPushConstants) == 80);
static_assert(offsetof(MeshPushConstants, materialIndex) == 64);
static_assert(sizeof(ShadowPushConstants) == 80);
static_assert(offsetof(ShadowPushConstants, lightIdx) == 64);
static_assert(offsetof(ShadowPushConstants, cascadeIndex) == 68);
static_assert(sizeof(SkyboxPushConstants) == 16);
static_assert(sizeof(DeferredLightingPushConstants) == 16);
static_assert(sizeof(ToneMappingPushConstants) == 16);
static_assert(offsetof(ToneMappingPushConstants, mode) == 4);
static_assert(offsetof(ToneMappingPushConstants, encodeSrgb) == 8);
