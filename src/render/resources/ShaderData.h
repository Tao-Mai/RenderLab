#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "render/RenderConfig.h"
#include "render/resources/LightRanges.h"

// Matches SceneUniforms in shaders/scene/scene_uniforms.slang.
struct alignas(16) SceneUniforms
{
    glm::mat4 viewProjection{1.0f};
    glm::mat4 inverseViewProjection{1.0f};
    glm::vec4 cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
    glm::mat4 view{1.0f};
};

struct alignas(16) SceneGlobals
{
    SceneUniforms camera;
    LightRanges lightRanges;
    float prefilteredMaxLod = 0.0f;
    float padding[3]{};
};

// GPU discriminator; values match shaders/scene/light_data.slang.
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
    float        spotFalloffExponent = 2.0f;
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
static_assert(sizeof(SceneGlobals) == 240);
static_assert(offsetof(SceneGlobals, lightRanges) == 208);
static_assert(offsetof(SceneGlobals, prefilteredMaxLod) == 224);
static_assert(sizeof(LightData) == 96);
static_assert(offsetof(LightData, color) == 0);
static_assert(offsetof(LightData, spotFalloffExponent) == 12);
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
    uint32_t  faceIndex    = 0;
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
    uint32_t mode       = 0;
    uint32_t encodeSrgb = 0;
    uint32_t padding[2]{};
};

static_assert(sizeof(MeshPushConstants) == 80);
static_assert(offsetof(MeshPushConstants, materialIndex) == 64);
static_assert(sizeof(ShadowPushConstants) == 80);
static_assert(offsetof(ShadowPushConstants, lightIdx) == 64);
static_assert(offsetof(ShadowPushConstants, cascadeIndex) == 68);
static_assert(sizeof(SkyboxPushConstants) == 16);
static_assert(sizeof(DeferredLightingPushConstants) == 16);
static_assert(sizeof(ToneMappingPushConstants) == 16);
static_assert(offsetof(ToneMappingPushConstants, mode) == 0);
static_assert(offsetof(ToneMappingPushConstants, encodeSrgb) == 4);
