#pragma once

#include "render/device/Device.h"

#include "render/feature/RenderGraph.h"

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
    void writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const;
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    RenderGraph::ImageHandle hdrColor;
    RenderGraph::ImageHandle color;
    DescriptorSetLayout      descriptorSetLayout;
    vk::raii::DescriptorSet  descriptorSet = nullptr;
    vk::Sampler              sampler;
    vk::Pipeline             pipeline;
    vk::PipelineLayout       pipelineLayout;
    bool                     encodeSrgb = false;
};
