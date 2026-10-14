#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "render/feature/RenderGraph.h"

#include <memory>
#include <utility>

class GpuTexture;

class SkyboxPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(
        RenderGraph&             graph,
        const PassSetup&         setup,
        RenderGraph::ImageHandle colorHandle,
        RenderGraph::ImageHandle depthHandle
    );
    void refreshBindings(const PassSetup& setup);
    void execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution) const;
    void setEnvironmentMap(EnvironmentMapAsset::ID id) { environmentMap = std::move(id); }

private:
    EnvironmentMapAsset::ID     environmentMap;
    RenderGraph::ImageHandle    color;
    RenderGraph::ImageHandle    depth;
    std::shared_ptr<GpuTexture> radianceTexture;
    vk::Pipeline                pipeline;
    vk::PipelineLayout          pipelineLayout;
};
