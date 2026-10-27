#pragma once

#include "render/RenderConfig.h"
#include "render/graph/RenderGraph.h"
#include "render/raytracing/AccelerationStructure.h"

#include <array>

class TlasBuildPass
{
public:
    struct Output
    {
        RenderGraph::AccelerationStructureHandle blas;
        RenderGraph::AccelerationStructureHandle tlas;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::AccelerationStructureHandle blas);
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame);

private:
    struct Frame
    {
        AccelerationStructure tlas;
        Buffer                instances;
        uint32_t              instanceCount = ~0u;
    };

    Output                               structures;
    std::array<Frame, maxFramesInFlight> frames;
};
