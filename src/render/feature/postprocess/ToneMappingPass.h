#pragma once

#include "render/device/Device.h"

#include "render/feature/RenderGraph.h"

#include <vector>

class ToneMappingPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(
        RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle hdrColor, RenderGraph::ImageHandle target
    );
    void prepare(RenderGraph& graph, const PassSetup& setup) const;
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    RenderGraph::ImageHandle             hdrColor;
    RenderGraph::ImageHandle             color;
    DescriptorSetLayout                  descriptorSetLayout;
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    vk::Sampler                          sampler;
    vk::Pipeline                         pipeline;
    PipelineLayout                       pipelineLayout;
    bool                                 encodeSrgb = false;
};
