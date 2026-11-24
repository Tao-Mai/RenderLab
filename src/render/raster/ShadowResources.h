#pragma once

#include <array>
#include <cstdint>
#include <slang-rhi.h>

class RenderData;
class RenderPassReflection;
class RenderResourceManager;

namespace rhi
{
struct ShaderCursor;
}

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
    rhi::ComPtr<rhi::IBuffer>                                       uniforms;
    rhi::ISampler*                                                  compareNearest;
    rhi::ISampler*                                                  compareLinear;
    rhi::ISampler*                                                  linearSampler;
    std::array<rhi::ComPtr<rhi::ITextureView>, kShadowPorts.size()> maps;
    std::array<rhi::ComPtr<rhi::IBuffer>, kShadowPorts.size()>      buffers;
};
