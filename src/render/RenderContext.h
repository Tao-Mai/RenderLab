#pragma once

#include <slang-rhi.h>
#include "render/RhiUtils.h"
#include "render/resources/ShaderManager.h"

#include <cstdint>

inline constexpr uint32_t kMaxFramesInFlight = 2;

class RenderResourceManager;

struct RenderContext
{
    rhi::ICommandEncoder*  commands      = nullptr;
    ShaderManager*         shaderManager = nullptr;
    RenderResourceManager* resources     = nullptr;
    uint32_t               frameIndex    = 0;
    float                  deltaSeconds  = 0.0f;
};

[[nodiscard]] inline rhi::RenderState viewportState(rhi::Viewport viewport)
{
    const rhi::ScissorRect scissor{
        uint32_t(viewport.originX),
        uint32_t(viewport.originY),
        uint32_t(viewport.originX + viewport.extentX),
        uint32_t(viewport.originY + viewport.extentY),
    };

    return {
        .viewports        = {viewport},
        .viewportCount    = 1,
        .scissorRects     = {scissor},
        .scissorRectCount = 1,
    };
}

[[nodiscard]] inline rhi::RenderState fullscreenState(rhi::Extent3D size)
{
    return viewportState(rhi::Viewport::fromSize(float(size.width), float(size.height)));
}
