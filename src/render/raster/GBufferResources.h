#pragma once

#include "render/graph/RenderGraph.h"

struct GBufferResources
{
    RenderGraph::ImageHandle diffuseOcclusion;
    RenderGraph::ImageHandle normalRoughness;
    RenderGraph::ImageHandle emissiveSpecularF90;
    RenderGraph::ImageHandle specularF0DiffuseFresnel;
    RenderGraph::ImageHandle sheenColorRoughness;
    RenderGraph::ImageHandle depth;
};
