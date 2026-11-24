#pragma once

#include <slang-rhi.h>

class RenderResourceManager;
struct GpuEnvironmentMap;
namespace rhi
{
struct ShaderCursor;
}

class SceneLighting
{
public:
    void init(rhi::IDevice& device, RenderResourceManager& resources);

    [[nodiscard]] rhi::IBuffer* getLightBuffer() const { return lights; }
    void
    bindRaster(const rhi::ShaderCursor& root, float ambientCoefficient, const GpuEnvironmentMap* environment) const;

private:
    rhi::ComPtr<rhi::IBuffer>      lights;
    rhi::ComPtr<rhi::ITextureView> brdfLut, sheenLut;
    rhi::ISampler*                 lutSampler = nullptr;
};
