#pragma once

#include "render/graph/RenderGraph.h"

struct RayTracingGeometry
{
    RenderGraph::BufferHandle objects;
    RenderGraph::BufferHandle triangles;
};

struct DenoiserInputs
{
    RenderGraph::ImageHandle depth;
    RenderGraph::ImageHandle worldNormal;
    RenderGraph::ImageHandle direct;
    RenderGraph::ImageHandle indirect;
    RenderGraph::ImageHandle emission;
    RenderGraph::ImageHandle albedo;
};
