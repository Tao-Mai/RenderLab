#pragma once

#include <slang-rhi.h>
#include "render/RenderConfig.h"
#include "render/device/GpuResources.h"

#include <array>

namespace Falcor
{
struct RenderContext;
}  // namespace Falcor

// Prepares shared hit records and expands emissive instances into world-space triangle lights.
class ShaderManager;

class RealtimeScene
{
public:
    void          compile(const Falcor::RenderContext& context, GpuResources::BufferHandle triangleBuffer = {});
    rhi::IBuffer* prepare(GpuResources& cache, const Falcor::RenderContext& frame);
    void          execute(rhi::ICommandEncoder& commands, const Falcor::RenderContext& frame);

    void bind(rhi::IShaderObject* object, GpuResources& cache, const Falcor::RenderContext& frame) const;
    [[nodiscard]] uint32_t emissiveTriangleCount() const { return triangleCount; }

private:
    struct Frame
    {
        rhi::ComPtr<rhi::IShaderObject> shaderObject;
    };

    GpuResources::BufferHandle            objects;
    GpuResources::BufferHandle            triangles;
    GpuResources::BufferHandle            tasks;
    uint32_t                              triangleCount = 0;
    uint32_t                              taskCount     = 0;
    rhi::ComPtr<rhi::IComputePipeline>    pipeline;
    std::array<Frame, kMaxFramesInFlight> frames;

    void createPipeline(rhi::IDevice& device, ShaderManager& shaders);
    void updateBindings(GpuResources& cache, const Falcor::RenderContext& frame);
};
