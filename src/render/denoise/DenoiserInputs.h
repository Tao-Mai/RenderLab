#pragma once

#include "render/graph/RenderGraph.h"

struct DenoiserInputs
{
    RenderGraph::ImageHandle depth;
    RenderGraph::ImageHandle worldNormal;
    RenderGraph::ImageHandle direct;
    RenderGraph::ImageHandle indirect;
    RenderGraph::ImageHandle emission;
    RenderGraph::ImageHandle albedo;
};
