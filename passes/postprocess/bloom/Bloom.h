#pragma once

#include "render/graph/RenderPass.h"

#include "passes/postprocess/Bloom/BloomSettings.h"
#include "render/graph/PassSettings.h"

#include "render/device/Device.h"

#include "render/graph/RenderGraph.h"

#include <memory>
#include <vector>

// Owns and connects the filter passes; only those passes enter the graph.
class Bloom : public ConfigurablePass<BloomSettings, CompositeRenderPass>
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;

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

        Output compile(
            RenderGraph&             graph,
            const PassSetup&         setup,
            RenderGraph::ImageHandle source,
            RenderGraph::ImageHandle base = {}
        );
        void bindResources(RenderGraph& graph, const PassSetup& setup) const;
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
