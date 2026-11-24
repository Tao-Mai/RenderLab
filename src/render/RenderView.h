#pragma once

#include "render/ViewportRect.h"
#include "render/resources/ShaderData.h"

struct RenderView
{
    SceneUniforms uniforms;
    ViewportRect  viewport;
};
