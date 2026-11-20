#pragma once

#include "passes/postprocess/Bloom/BloomSettings.h"
#include "render/graph/PassSettings.h"

#include "render/device/Device.h"

#include "render/graph/RenderGraph.h"

#include <memory>
#include <vector>

// Owns and connects the filter passes; only those passes enter the graph.
class Bloom : public ConfigurablePass<BloomSettings>
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
            DownsampleHorizontal,
            DownsampleVertical,
            Upsample,
            Composite
        };

        FilterPass(Stage stage, const BloomSettings& settings) : stage(stage), settings(settings) {}

        Output setup(
            RenderGraph&             graph,
            const PassSetup&         setup,
            RenderGraph::ImageHandle source,
            RenderGraph::ImageHandle base = {}
        );
        void writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const;
        void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

    private:
        Stage                    stage;
        const BloomSettings&     settings;
        RenderGraph::ImageHandle source;
        RenderGraph::ImageHandle base;
        RenderGraph::ImageHandle color;
        DescriptorSetLayout      descriptorSetLayout;
        vk::raii::DescriptorSet  descriptorSet = nullptr;
        vk::Sampler              sampler;
        vk::Pipeline             pipeline;
        vk::PipelineLayout       pipelineLayout;
    };

    std::vector<std::unique_ptr<FilterPass>> filters;
};
