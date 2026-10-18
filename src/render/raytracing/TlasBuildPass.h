#pragma once

#include "render/RenderConfig.h"
#include "render/raytracing/BlasBuildPass.h"

#include <array>

class TlasBuildPass
{
public:
    struct Output
    {
        RenderGraph::AccelerationStructureHandle blas;
        RenderGraph::AccelerationStructureHandle tlas;
    };

    explicit TlasBuildPass(const BlasBuildPass& bottomLevels) : bottomLevels(bottomLevels) {}

    Output setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::AccelerationStructureHandle blas);
    void   execute(RenderGraph& graph, CommandRecorder& commands, const PassFrame& frame);

    [[nodiscard]] const AccelerationStructure& structure(uint32_t frameIndex) const { return frames[frameIndex].tlas; }

private:
    struct Frame
    {
        AccelerationStructure tlas;
        Buffer                instances;
        uint32_t              instanceCount = ~0u;
    };

    const BlasBuildPass&                 bottomLevels;
    std::array<Frame, maxFramesInFlight> frames;
};
