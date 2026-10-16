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
inline constexpr uint32_t kMaterialSet = 1;
inline constexpr uint32_t kObjectSet   = 2;
inline constexpr uint32_t kPassSet     = 3;

inline constexpr uint32_t kGBufferAlbedoBinding   = 0;
inline constexpr uint32_t kGBufferNormalBinding   = 1;
inline constexpr uint32_t kGBufferEmissiveBinding = 2;
inline constexpr uint32_t kGBufferDepthBinding    = 3;
inline constexpr uint32_t kGBufferSheenBinding    = 4;

// Scene data: UBOs, then SSBOs.
inline constexpr uint32_t kViewUniformBinding             = 0;
inline constexpr uint32_t kLightUniformBinding            = 1;
inline constexpr uint32_t kLightBufferBinding             = 2;
inline constexpr uint32_t kPointShadowBufferBinding       = 3;
inline constexpr uint32_t kSpotShadowBufferBinding        = 4;
inline constexpr uint32_t kRectAreaShadowBufferBinding    = 5;
inline constexpr uint32_t kDirectionalShadowBufferBinding = 6;

// Shadow images, then comparison and regular samplers.
inline constexpr uint32_t kPointShadowImageBinding        = 7;
inline constexpr uint32_t kSpotShadowImageBinding         = 8;
inline constexpr uint32_t kRectAreaShadowImageBinding     = 9;
inline constexpr uint32_t kDirectionalShadowImageBinding  = 10;
inline constexpr uint32_t kShadowComparisonSamplerBinding = 11;
inline constexpr uint32_t kLinearSamplerBinding           = 12;

// Environment textures: adjacent image / sampler pairs.
inline constexpr uint32_t kIrradianceImageBinding            = 13;
inline constexpr uint32_t kIrradianceSamplerBinding          = 14;
inline constexpr uint32_t kPrefilteredSpecularImageBinding   = 15;
inline constexpr uint32_t kPrefilteredSpecularSamplerBinding = 16;
inline constexpr uint32_t kBrdfLutImageBinding               = 17;
inline constexpr uint32_t kBrdfLutSamplerBinding             = 18;
inline constexpr uint32_t kRadianceImageBinding              = 19;
inline constexpr uint32_t kRadianceSamplerBinding            = 20;
inline constexpr uint32_t kSheenLutImageBinding              = 21;
inline constexpr uint32_t kSheenLutSamplerBinding            = 22;

// Material set.
inline constexpr uint32_t kMaterialImageBinding                     = 0;
inline constexpr uint32_t kMaterialSamplerBinding                   = 1;
inline constexpr uint32_t kMaterialUniformBinding                   = 2;
inline constexpr uint32_t kMaterialNormalImageBinding               = 3;
inline constexpr uint32_t kMaterialNormalSamplerBinding             = 4;
inline constexpr uint32_t kMaterialMetallicImageBinding             = 5;
inline constexpr uint32_t kMaterialMetallicSamplerBinding           = 6;
inline constexpr uint32_t kMaterialRoughnessImageBinding            = 7;
inline constexpr uint32_t kMaterialRoughnessSamplerBinding          = 8;
inline constexpr uint32_t kMaterialEmissiveImageBinding             = 9;
inline constexpr uint32_t kMaterialEmissiveSamplerBinding           = 10;
inline constexpr uint32_t kMaterialOcclusionImageBinding            = 11;
inline constexpr uint32_t kMaterialOcclusionSamplerBinding          = 12;
inline constexpr uint32_t kMaterialClearcoatImageBinding            = 13;
inline constexpr uint32_t kMaterialClearcoatSamplerBinding          = 14;
inline constexpr uint32_t kMaterialClearcoatRoughnessImageBinding   = 15;
inline constexpr uint32_t kMaterialClearcoatRoughnessSamplerBinding = 16;
inline constexpr uint32_t kMaterialClearcoatNormalImageBinding      = 17;
inline constexpr uint32_t kMaterialClearcoatNormalSamplerBinding    = 18;
inline constexpr uint32_t kMaterialSheenColorImageBinding           = 19;
inline constexpr uint32_t kMaterialSheenColorSamplerBinding         = 20;
inline constexpr uint32_t kMaterialSheenRoughnessImageBinding       = 21;
inline constexpr uint32_t kMaterialSheenRoughnessSamplerBinding     = 22;

// Tone mapping uses its own set 0, not the scene layout.
inline constexpr uint32_t kToneMappingImageBinding   = 0;
inline constexpr uint32_t kToneMappingSamplerBinding = 1;
}  // namespace RenderInterface

// Matches the std140 ViewUniforms block in shaders/scene_data.slang.
struct alignas(16) ViewUniforms
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
    bool operator==(const LightRanges&) const = default;
    uint32_t directionalEnd = 0;
    uint32_t pointEnd       = 0;
    uint32_t rectAreaEnd    = 0;
    uint32_t spotEnd        = 0;
};
static_assert(sizeof(LightRanges) == 16);

// The SSBO contains lightRanges.spotEnd active entries. IBL.x is
// the maximum LOD of the scene's prefiltered specular cubemap. shadowParameters.x
// is the normalized shadow depth bias, y is the point-shadow direction-space PCF radius,
// z enables per-pixel rotation (0/1), and w is the spot-shadow PCF radius in texels.
struct alignas(16) LightUniforms
{
    LightRanges lightRanges;
    float     ambientCoefficient            = 0.1f;
    uint32_t  cascadeCount                  = 0;
    float     directionalShadowSampleRadius = 0.0f;  // Shadow-map texels.
    float     padding = 0.0f;
    glm::vec4 iblParameters{0.0f};
    glm::vec4 shadowParameters{0.0f};
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
    glm::vec3  color{0.0f};
    float      padding3 = 0.0f;
    glm::vec4  positionRange{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4  direction{0.0f, -1.0f, 0.0f, 0.0f};
    glm::vec4  areaSizeCone{1.0f, 1.0f, 0.9396926f, 0.8660254f};
    GpuLightType lightType = GpuLightType::Point;
    uint32_t   shadowEnabled = 0;
    uint32_t   shadowidx = ~0u;
    uint32_t   padding0  = 0;
    glm::vec4  areaRightData{1.0f, 0.0f, 0.0f, 0.0f};

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

struct alignas(16) MaterialUniforms
{
    glm::vec4 baseColorFactor{1.0f};
    float     roughness         = 1.0f;
    float     metallic          = 1.0f;
    float     alphaCutoff       = 0.5f;
    uint32_t  alphaMode         = 0;
    float     normalScale       = 1.0f;
    float     occlusionStrength = 1.0f;
    float     padding1          = 0.0f;
    float     padding2          = 0.0f;
    glm::vec4 emissiveFactor{0.0f};
    glm::vec3 sheenColorFactor{0.0f};
    float     sheenRoughnessFactor = 0.0f;
    // Optional shader-variant tail; ordinary materials only upload the prefix.
    float clearcoatFactor          = 0.0f;
    float clearcoatRoughnessFactor = 0.0f;
    float clearcoatNormalScale     = 1.0f;
    float clearcoatPadding         = 0.0f;
};

static_assert(sizeof(ViewUniforms) == 208);
static_assert(offsetof(ViewUniforms, inverseViewProjection) == 64);
static_assert(offsetof(ViewUniforms, cameraPosition) == 128);
static_assert(offsetof(ViewUniforms, view) == 144);
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
static_assert(offsetof(LightData, shadowidx) == 72);
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
static_assert(sizeof(MaterialUniforms) == 96);
static_assert(offsetof(MaterialUniforms, sheenColorFactor) == 64);
static_assert(offsetof(MaterialUniforms, sheenRoughnessFactor) == 76);
static_assert(offsetof(MaterialUniforms, clearcoatFactor) == 80);
static_assert(offsetof(MaterialUniforms, clearcoatRoughnessFactor) == 84);
static_assert(offsetof(MaterialUniforms, clearcoatNormalScale) == 88);
static_assert(offsetof(MaterialUniforms, roughness) == 16);
static_assert(offsetof(MaterialUniforms, metallic) == 20);
static_assert(offsetof(MaterialUniforms, alphaCutoff) == 24);
static_assert(offsetof(MaterialUniforms, alphaMode) == 28);
static_assert(offsetof(MaterialUniforms, normalScale) == 32);
static_assert(offsetof(MaterialUniforms, occlusionStrength) == 36);
static_assert(offsetof(MaterialUniforms, emissiveFactor) == 48);

struct MeshPushConstants
{
    glm::mat4 model{1.0f};
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

static_assert(sizeof(MeshPushConstants) == 64);
static_assert(sizeof(ShadowPushConstants) == 80);
static_assert(offsetof(ShadowPushConstants, lightIdx) == 64);
static_assert(offsetof(ShadowPushConstants, cascadeIndex) == 68);
static_assert(sizeof(SkyboxPushConstants) == 16);
static_assert(sizeof(DeferredLightingPushConstants) == 16);
static_assert(sizeof(ToneMappingPushConstants) == 16);
static_assert(offsetof(ToneMappingPushConstants, mode) == 4);
static_assert(offsetof(ToneMappingPushConstants, encodeSrgb) == 8);
