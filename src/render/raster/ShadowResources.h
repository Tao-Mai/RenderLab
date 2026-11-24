#pragma once

#include <slang-rhi.h>
#include <array>

struct FrameData;
class RenderResourceManager;
namespace Falcor
{
class RenderData;
class RenderPassReflection;
}

struct ShadowPorts
{
    const char* map;
    const char* data;
};

inline constexpr ShadowPorts kPointShadowPorts{"pointShadowMap", "pointShadowData"};
inline constexpr ShadowPorts kSpotShadowPorts{"spotShadowMap", "spotShadowData"};
inline constexpr ShadowPorts kRectAreaShadowPorts{"rectAreaShadowMap", "rectAreaShadowData"};
inline constexpr ShadowPorts kDirectionalShadowPorts{"cascadeShadowMap", "directionalShadowData"};
inline constexpr std::array kShadowPorts{
    kPointShadowPorts, kSpotShadowPorts, kRectAreaShadowPorts, kDirectionalShadowPorts
};

void addShadowInputs(Falcor::RenderPassReflection& reflection);

class ShadowBindings
{
public:
    ShadowBindings(const Falcor::RenderData& data, const FrameData& frame, RenderResourceManager& resources);
    void bind(rhi::IShaderObject* object) const;

private:
    const FrameData& frame;
    rhi::ISampler* linearSampler;
    std::array<rhi::ComPtr<rhi::ITextureView>, kShadowPorts.size()> maps;
    std::array<rhi::ComPtr<rhi::IBuffer>, kShadowPorts.size()> buffers;
};
