#pragma once

#include "render/device/Device.h"
#include "render/graph/RenderGraph.h"

#include <array>
#include <memory>
#include <vector>

// Connects Falcor's SimplePostFX and ToneMapper, followed by swapchain presentation.
class FalcorPostProcess
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output
    setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle source, RenderGraph::ImageHandle target);

private:
    enum class Stage
    {
        Downsample,
        Upsample,
        PostFx,
        Luminance,
        LuminanceMip,
        ToneMapping,
        Present,
    };

    class ImagePass
    {
    public:
        ImagePass(FalcorPostProcess& owner, Stage stage, uint32_t level) : owner(owner), stage(stage), level(level) {}
        Output setup(
            RenderGraph&             graph,
            const PassSetup&         setup,
            RenderGraph::ImageHandle source,
            RenderGraph::ImageHandle filtered,
            RenderGraph::ImageHandle target
        );
        void writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const;
        void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame) const;

    private:
        [[nodiscard]] bool isCompute() const { return stage <= Stage::PostFx; }
        void dispatch(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame) const;

        FalcorPostProcess&       owner;
        Stage                    stage;
        uint32_t                 level;
        RenderGraph::ImageHandle source;
        RenderGraph::ImageHandle filtered;
        RenderGraph::ImageHandle color;
        vk::raii::DescriptorSet  descriptorSet = nullptr;
        vk::Pipeline             pipeline;
    };

    void initialize(const PassSetup& setup);

    DescriptorSetLayout               computeDescriptorSetLayout;
    DescriptorSetLayout               graphicsDescriptorSetLayout;
    vk::PipelineLayout                computePipelineLayout;
    vk::PipelineLayout                graphicsPipelineLayout;
    vk::raii::Sampler                 borderSampler = nullptr;
    vk::raii::Sampler                 linearSampler = nullptr;
    vk::raii::Sampler                 pointSampler  = nullptr;
    std::array<vk::raii::Pipeline, 3> computePipelines{
        vk::raii::Pipeline{nullptr}, vk::raii::Pipeline{nullptr}, vk::raii::Pipeline{nullptr}
    };
    bool                                    encodeSrgb = false;
    std::vector<std::unique_ptr<ImagePass>> imagePasses;
};
