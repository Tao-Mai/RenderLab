#pragma once
#include <slang-rhi.h>
#include "core/Macros.h"
#include "render/device/RhiCheck.h"
#include "render/resources/ShaderManager.h"

#include <functional>
#include <vector>

class RenderResourceManager;
class GpuScene;
struct EditorFrameInput;
struct PassEditorContext;

struct RENDERLAB_CORE_API RenderContext
{
    rhi::IDevice*            device        = nullptr;
    rhi::ICommandEncoder*    commands      = nullptr;
    ShaderManager*           shaderManager = nullptr;
    RenderResourceManager*   resources     = nullptr;
    GpuScene*                scene         = nullptr;
    const EditorFrameInput*  editor        = nullptr;
    uint32_t                 frameIndex    = 0;
    float                    deltaSeconds  = 0.0f;

    // Passes register callbacks here while recording. The submitter runs them after the fence.
    std::vector<std::function<void()>> onGpuComplete;

    void notifyGpuComplete()
    {
        for (const auto& complete : onGpuComplete)
            complete();
        onGpuComplete.clear();
    }
};
