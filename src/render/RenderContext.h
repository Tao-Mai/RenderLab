#pragma once

#include "render/ComputeContext.h"
#include "render/FrameData.h"
#include "render/resources/Resource.h"

#include <cstdint>
#include <span>

class ShaderManager;
class RenderResourceManager;
class MaterialTable;
class OfflineTracePipelineCache;
class GpuProfiler;
class Profiler;
class VulkanContext;
class AccelerationStructure;
struct RenderView;
struct PreparedScene;
struct EditorFrameInput;
struct RendererConfig;
struct PassEditorContext;

namespace Falcor
{
// The renderer provides the services and frame data used during command recording.
class FALCOR_API RenderContext : public ComputeContext
{
public:
    using ComputeContext::ComputeContext;

    void resolveResource(const ref<Texture>& source, const ref<Texture>& target);
    void clearTexture(Texture* texture, const float4& clearColor = float4(0, 0, 0, 1));

    ShaderManager*               shaderManager = nullptr;
    RenderResourceManager*       resources        = nullptr;
    MaterialTable*               materialTable    = nullptr;
    OfflineTracePipelineCache*   offlinePipelines = nullptr;
    const VulkanContext*         vulkan           = nullptr;
    const RendererConfig*        config           = nullptr;
    std::span<FrameData>         frames;
    FrameData*                   frameData       = nullptr;
    const RenderView*            view            = nullptr;
    const PreparedScene*         scene           = nullptr;
    const EditorFrameInput*      editor          = nullptr;
    const PassEditorContext*     passEditor      = nullptr;
    const AccelerationStructure* sceneTlas       = nullptr;
    GpuProfiler*                 gpuProfiler     = nullptr;
    Profiler*                    profiler        = nullptr;
    uint32_t                     frameIndex      = 0;
    float                        deltaSeconds    = 0.0f;
};
}  // namespace Falcor
