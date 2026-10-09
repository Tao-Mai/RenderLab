#pragma once

#include "render/ViewportRect.h"
#include "render/resource/ShaderData.h"

#include <cstdint>

struct RenderView
{
    ViewUniforms uniforms;
    ViewportRect viewport;
};

struct RenderFrameResult
{
    uint32_t pickedSelectionId     = 0;
    bool     hasPickResult         = false;
    bool     swapchainNeedsRebuild = false;
};
