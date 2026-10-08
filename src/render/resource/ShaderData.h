#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "scene/component/LightComponent.h"
#include "render/RenderConfig.h"

namespace RenderInterface
{
inline constexpr uint32_t kSceneSet    = 0;
inline constexpr uint32_t kMaterialSet = 1;
inline constexpr uint32_t kObjectSet   = 2;
inline constexpr uint32_t kPassSet     = 3;

// Scene data: UBOs, then SSBOs.
inline constexpr uint32_t kViewUniformBinding          = 0;
inline constexpr uint32_t kLightUniformBinding         = 1;
inline constexpr uint32_t kLightBufferBinding          = 2;
inline constexpr uint32_t kPointShadowBufferBinding    = 3;
inline constexpr uint32_t kRectAreaShadowBufferBinding = 4;
inline constexpr uint32_t kDirectionalShadowBufferBinding = 5;

// Shadow images, then comparison and regular samplers.
inline constexpr uint32_t kPointShadowImageBinding        = 6;
inline constexpr uint32_t kRectAreaShadowImageBinding     = 7;
inline constexpr uint32_t kDirectionalShadowImageBinding  = 8;
inline constexpr uint32_t kShadowComparisonSamplerBinding = 9;
inline constexpr uint32_t kLinearSamplerBinding           = 10;

// Environment textures: adjacent image / sampler pairs.
inline constexpr uint32_t kIrradianceImageBinding            = 11;
inline constexpr uint32_t kIrradianceSamplerBinding          = 12;
inline constexpr uint32_t kPrefilteredSpecularImageBinding   = 13;
inline constexpr uint32_t kPrefilteredSpecularSamplerBinding = 14;
inline constexpr uint32_t kBrdfLutImageBinding               = 15;
inline constexpr uint32_t kBrdfLutSamplerBinding             = 16;
inline constexpr uint32_t kRadianceImageBinding              = 17;
inline constexpr uint32_t kRadianceSamplerBinding            = 18;

// Material set.
inline constexpr uint32_t kMaterialImageBinding           = 0;
inline constexpr uint32_t kMaterialSamplerBinding         = 1;
inline constexpr uint32_t kMaterialUniformBinding         = 2;
inline constexpr uint32_t kMaterialNormalImageBinding     = 3;
inline constexpr uint32_t kMaterialNormalSamplerBinding   = 4;
inline constexpr uint32_t kMaterialMetallicImageBinding   = 5;
inline constexpr uint32_t kMaterialMetallicSamplerBinding = 6;
inline constexpr uint32_t kMaterialRoughnessImageBinding   = 7;
inline constexpr uint32_t kMaterialRoughnessSamplerBinding = 8;
inline constexpr uint32_t kMaterialEmissiveImageBinding   = 9;
inline constexpr uint32_t kMaterialEmissiveSamplerBinding = 10;
inline constexpr uint32_t kMaterialOcclusionImageBinding   = 11;
inline constexpr uint32_t kMaterialOcclusionSamplerBinding = 12;

// Tone mapping uses its own set 0, not the scene layout.
inline constexpr uint32_t kToneMappingImageBinding = 0;
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

// The SSBO contains lightCount entries, including disabled lights. IBL.x is
// the maximum LOD of the scene's prefiltered specular cubemap. shadowParameters.x
// is the normalized shadow depth bias, y is the point-shadow direction-space PCF radius,
// z enables per-pixel rotation (0/1), and w is reserved.
struct alignas(16) LightUniforms
{
    uint32_t  lightCount = 0;
    float     ambientCoefficient = 0.1f;
    uint32_t  cascadeCount = 0;
    uint32_t  padding2   = 0;
    glm::vec4 iblParameters{0.0f};
    glm::vec4 shadowParameters{0.0f};
};

// One std430 SSBO element, with a 112-byte array stride.
// flags = {light type, enabled, castShadow, reserved}.
struct alignas(16) LightData
{
    glm::vec4  colorIntensity{1.0f, 1.0f, 1.0f, 0.0f};
    glm::vec4  positionRange{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4  direction{0.0f, -1.0f, 0.0f, 0.0f};
    glm::vec4  areaSizeCone{1.0f, 1.0f, 0.9396926f, 0.8660254f};
    glm::uvec4 flags{0u};
    uint32_t   shadowidx = ~0u;
    uint32_t   padding0  = 0;
    uint32_t   padding1  = 0;
    uint32_t   padding2  = 0;
    glm::vec4  areaRightData{1.0f, 0.0f, 0.0f, 0.0f};

    [[nodiscard]] glm::vec3 areaRight() const { return xyz(areaRightData); }

    [[nodiscard]] glm::vec3 position() const
    {
        return xyz(positionRange);
    }

    [[nodiscard]] float range() const
    {
        return positionRange.w;
    }

    [[nodiscard]] LightComponent::Type type() const
    {
        return static_cast<LightComponent::Type>(flags.x);
    }

    [[nodiscard]] bool enable() const
    {
        return flags.y;
    }

    [[nodiscard]] bool castShadow() const
    {
        return flags.z;
    }
};

struct alignas(16) PointShadowData
{
    std::array<glm::mat4, 6> vp;
};

struct alignas(16) RectAreaShadowData
{
    glm::mat4 vp{1.0f};
    float     nearPlane = 0.0f;
    float     farPlane  = 0.0f;
    // World-space radius at light-view depth z maps to radius * worldToUvScale / z.
    glm::vec2 worldToUvScale{0.0f};
};

struct alignas(16) CascadeShadowData
{
    glm::mat4 vp{1.0f};
    float     depthUpperBound = 0.0f;  // Positive camera-view depth, not distance to the camera.
    float     padding0 = 0.0f;
    float     padding1 = 0.0f;
    float     padding2 = 0.0f;
};

struct alignas(16) DirectionalShadowData
{
    std::array<CascadeShadowData, kMaxDirectionalShadowCascades> cascades;
};

struct alignas(16) MaterialUniforms
{
    glm::vec4 baseColorFactor{1.0f};
    float     roughness   = 1.0f;
    float     metallic    = 1.0f;
    float     alphaCutoff = 0.5f;
    uint32_t  alphaMode   = 0;
    float     normalScale = 1.0f;
    float     occlusionStrength = 1.0f;
    float     padding1 = 0.0f;
    float     padding2 = 0.0f;
    glm::vec4 emissiveFactor{0.0f};
};

static_assert(sizeof(ViewUniforms) == 208);
static_assert(offsetof(ViewUniforms, inverseViewProjection) == 64);
static_assert(offsetof(ViewUniforms, cameraPosition) == 128);
static_assert(offsetof(ViewUniforms, view) == 144);
static_assert(sizeof(LightUniforms) == 48);
static_assert(offsetof(LightUniforms, ambientCoefficient) == 4);
static_assert(offsetof(LightUniforms, cascadeCount) == 8);
static_assert(offsetof(LightUniforms, iblParameters) == 16);
static_assert(offsetof(LightUniforms, shadowParameters) == 32);
static_assert(sizeof(LightData) == 112);
static_assert(offsetof(LightData, positionRange) == 16);
static_assert(offsetof(LightData, direction) == 32);
static_assert(offsetof(LightData, areaSizeCone) == 48);
static_assert(offsetof(LightData, flags) == 64);
static_assert(offsetof(LightData, shadowidx) == 80);
static_assert(offsetof(LightData, areaRightData) == 96);
static_assert(sizeof(PointShadowData) == 384);
static_assert(offsetof(PointShadowData, vp) == 0);
static_assert(sizeof(RectAreaShadowData) == 80);
static_assert(offsetof(RectAreaShadowData, vp) == 0);
static_assert(offsetof(RectAreaShadowData, nearPlane) == 64);
static_assert(offsetof(RectAreaShadowData, farPlane) == 68);
static_assert(offsetof(RectAreaShadowData, worldToUvScale) == 72);
static_assert(sizeof(CascadeShadowData) == 80);
static_assert(offsetof(CascadeShadowData, depthUpperBound) == 64);
static_assert(sizeof(DirectionalShadowData) == kMaxDirectionalShadowCascades * sizeof(CascadeShadowData));
static_assert(sizeof(MaterialUniforms) == 64);
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
    uint32_t  lightIdx = 0;
    uint32_t  cascadeIndex = 0;
    uint32_t  padding1 = 0;
    uint32_t  padding2 = 0;
};

struct LightPushConstants
{
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

struct EditorPickingPushConstants
{
    glm::mat4 model{1.0f};
    uint32_t  selectionId = 0;
    uint32_t  padding0    = 0;
    uint32_t  padding1    = 0;
    uint32_t  padding2    = 0;
};

struct SkyboxPushConstants
{
    glm::vec4 viewport{0.0f};
};

struct ToneMappingPushConstants
{
    float exposure = 0.0f;
    uint32_t mode = 0;
    uint32_t encodeSrgb = 0;
    uint32_t padding = 0;
};

static_assert(sizeof(MeshPushConstants) == 64);
static_assert(sizeof(ShadowPushConstants) == 80);
static_assert(offsetof(ShadowPushConstants, lightIdx) == 64);
static_assert(offsetof(ShadowPushConstants, cascadeIndex) == 68);
static_assert(sizeof(LightPushConstants) == 80);
static_assert(sizeof(EditorPickingPushConstants) == 80);
static_assert(sizeof(SkyboxPushConstants) == 16);
static_assert(sizeof(ToneMappingPushConstants) == 16);
static_assert(offsetof(ToneMappingPushConstants, mode) == 4);
static_assert(offsetof(ToneMappingPushConstants, encodeSrgb) == 8);
