#pragma once

#include "render/feature/RenderGraph.h"

#include <vector>

class ToneMappingPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(RenderGraph& graph, RenderGraph::ImageHandle hdrColor, RenderGraph::ImageHandle target);
    void   prepare(RenderGraph& graph) const;
    void execute(RenderGraph& graph) const;

private:
    RenderGraph::ImageHandle hdrColor;
    RenderGraph::ImageHandle color;
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    vk::Sampler sampler;
    vk::Pipeline pipeline;
    vk::PipelineLayout pipelineLayout;
    bool encodeSrgb = false;
};
