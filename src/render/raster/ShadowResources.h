#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <slang-rhi.h>

class RenderData;
class RenderPassReflection;
class RenderResourceManager;

namespace rhi
{
struct ShaderCursor;
}

inline constexpr uint32_t kMaxDirectionalLightShadow = 1;
inline constexpr uint32_t kMaxDirectionalShadowCascades = 4;
inline constexpr uint32_t kMaxPointLightShadow = 4;
inline constexpr uint32_t kMaxSpotLightShadow = 4;
inline constexpr uint32_t kMaxRectAreaLightShadow = 4;

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

struct alignas(16) ShadowUniforms
{
    uint32_t cascadeCount;
    float    directionalSampleRadius;
    float    bias;
    float    sampleRadius;
    float    spotSampleRadius;
    uint32_t pixelRotation;
    uint32_t enabled;
    uint32_t linearFilter;
};
static_assert(sizeof(ShadowUniforms) == 32);

inline constexpr char kShadowUniformsPort[] = "shadowUniforms";

struct ShadowPorts
{
    const char* map;
    const char* data;
};

inline constexpr ShadowPorts kPointShadowPorts{"pointShadowMap", "pointShadowData"};
inline constexpr ShadowPorts kSpotShadowPorts{"spotShadowMap", "spotShadowData"};
inline constexpr ShadowPorts kRectAreaShadowPorts{"rectAreaShadowMap", "rectAreaShadowData"};
inline constexpr ShadowPorts kDirectionalShadowPorts{"cascadeShadowMap", "directionalShadowData"};
inline constexpr std::array  kShadowPorts{
    kPointShadowPorts, kSpotShadowPorts, kRectAreaShadowPorts, kDirectionalShadowPorts
};

void addShadowInputs(RenderPassReflection& reflection);

class ShadowBindings
{
public:
    ShadowBindings(const RenderData& data, RenderResourceManager& resources);
    void bind(const rhi::ShaderCursor& root) const;

private:
    rhi::IBuffer*                                   uniforms;
    rhi::ISampler*                                  compareNearest;
    rhi::ISampler*                                  compareLinear;
    rhi::ISampler*                                  linearSampler;
    std::array<rhi::ITexture*, kShadowPorts.size()> maps;
    std::array<rhi::IBuffer*, kShadowPorts.size()>  buffers;
};
