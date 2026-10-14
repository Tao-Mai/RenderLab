#pragma once

#include "render/feature/RenderGraph.h"

class BloomPass
{
public:
    enum class Stage : uint32_t
    {
        Downsample,
        Upsample,
        Composite
    };

    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    explicit BloomPass(Stage stage) : stage(stage) {}

    static RenderGraph::ImageHandle
    setupChain(RenderGraph& graph, RenderGraph::ImageHandle hdrColor, std::vector<std::unique_ptr<BloomPass>>& storage);
    Output setup(RenderGraph& graph, RenderGraph::ImageHandle source, RenderGraph::ImageHandle base = {});
    void   prepare(RenderGraph& graph) const;
    void   execute(RenderGraph& graph) const;

private:
    Stage                                stage;
    RenderGraph::ImageHandle             source;
    RenderGraph::ImageHandle             base;
    RenderGraph::ImageHandle             color;
    std::vector<vk::raii::DescriptorSet> descriptorSets;
    vk::Sampler                          sampler;
    vk::Pipeline                         pipeline;
    vk::PipelineLayout                   pipelineLayout;
};
