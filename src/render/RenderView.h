#pragma once

#include "render/ViewportRect.h"
#include "render/resources/ShaderData.h"

#include <cstdint>

struct RenderView
{
    SceneUniforms uniforms;
    ViewportRect  viewport;
};

struct RenderFrameResult
{
    uint32_t pickedSelectionId     = 0;
    bool     hasPickResult         = false;
    bool     swapchainNeedsRebuild = false;
};
