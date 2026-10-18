#pragma once

#include "render/feature/RenderGraph.h"

class BloomPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle hdrColor);

private:
    class FilterPass
    {
    public:
        enum class Stage : uint32_t
        {
            Downsample,
            Upsample,
            Composite
        };

        explicit FilterPass(Stage stage) : stage(stage) {}

        Output setup(
            RenderGraph&             graph,
            const PassSetup&         setup,
            RenderGraph::ImageHandle source,
            RenderGraph::ImageHandle base = {}
        );
        void prepare(RenderGraph& graph, const PassSetup& setup) const;
        void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

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

    std::vector<std::unique_ptr<FilterPass>> filters;
};
