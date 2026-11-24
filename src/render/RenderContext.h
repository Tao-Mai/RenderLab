#pragma once
#include <slang-rhi.h>
#include "core/Macros.h"
#include "core/math/Vector.h"
#include "render/FrameData.h"
#include "render/device/RhiCheck.h"
#include "render/resources/ShaderManager.h"
class ShaderManager;
class RenderResourceManager;
class MaterialTable;
class OfflineTracePipelineCache;
class GpuResources;
class GpuProfiler;
class Profiler;
struct RenderView;
struct PreparedScene;
struct EditorFrameInput;
struct RendererConfig;
struct PassEditorContext;

namespace Falcor
{
struct FALCOR_API RenderContext
{
    rhi::IDevice*                device           = nullptr;
    rhi::ICommandQueue*          queue            = nullptr;
    rhi::ICommandEncoder*        commands         = nullptr;
    GpuResources*                gpuResources     = nullptr;
    ShaderManager*               shaderManager    = nullptr;
    RenderResourceManager*       resources        = nullptr;
    MaterialTable*               materialTable    = nullptr;
    OfflineTracePipelineCache*   offlinePipelines = nullptr;
    const RendererConfig*        config           = nullptr;
    FrameData*                   frameData    = nullptr;
    const RenderView*            view         = nullptr;
    const PreparedScene*         scene        = nullptr;
    const EditorFrameInput*      editor       = nullptr;
    const PassEditorContext*     passEditor   = nullptr;
    rhi::IAccelerationStructure* sceneTlas    = nullptr;
    GpuProfiler*                 gpuProfiler  = nullptr;
    Profiler*                    profiler     = nullptr;
    uint32_t                     frameIndex   = 0;
    float                        deltaSeconds = 0.0f;
};
}  // namespace Falcor
