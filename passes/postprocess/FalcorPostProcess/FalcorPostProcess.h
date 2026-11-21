#pragma once

#include "render/graph/RenderPass.h"

#include "passes/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h"
#include "render/graph/PassSettings.h"

#include "render/device/Device.h"
#include "render/raster/RasterAttachments.h"

#include <memory>
#include <vector>

// Connects Falcor's SimplePostFX and ToneMapper, followed by swapchain presentation.
class FalcorPostProcess : public ConfigurablePass<FalcorPostProcessConfig, CompositeRenderPass>
{
public:
    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;

private:
    enum class Stage
    {
        Downsample,
        Upsample,
        PostFx,
        Luminance,
        ToneMapping,
        Present,
    };

    struct Layout
    {
        DescriptorSetLayout descriptorSet;
        vk::PipelineLayout pipeline;
    };

    class ImagePass
    {
    public:
        ImagePass(const FalcorPostProcess& owner, Stage stage, const ComputePipeline& compute, uint32_t level = 0) :
            owner(owner), layout(owner.computeLayout), stage(stage), compute(&compute), level(level)
        {
        }

        ImagePass(const FalcorPostProcess& owner, Stage stage, ShaderHandle shader) :
            owner(owner), layout(owner.graphicsLayout), stage(stage), shader(shader)
        {
        }

        RenderGraph::ImageHandle compile(
            RenderGraph&             graph,
            const PassSetup&         setup,
            RenderGraph::ImageHandle source,
            RenderGraph::ImageHandle filtered,
            RenderGraph::ImageHandle target
        );
        void bindResources(RenderGraph& graph, const PassSetup& setup) const;
        void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame) const;

    private:
        void dispatch(RenderGraph& graph, vk::raii::CommandBuffer& commands) const;

        const FalcorPostProcess& owner;
        const Layout&            layout;
        Stage                    stage;
        const ComputePipeline*   compute = nullptr;
        ShaderHandle             shader;
        uint32_t                 level = 0;
        RenderGraph::ImageHandle source;
        RenderGraph::ImageHandle filtered;
        RenderGraph::ImageHandle storageTarget;
        RasterAttachments<1>     attachments;
        vk::raii::DescriptorSet  descriptorSet = nullptr;
        vk::Pipeline             pipeline;
    };

    void initialize(const PassSetup& setup);
    RenderGraph::ImageHandle addSimplePostFx(
        RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle source
    );
    RenderGraph::ImageHandle addToneMapper(
        RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle source
    );
    RenderGraph::ImageHandle addImagePass(
        RenderGraph& graph, const PassSetup& setup, ImagePass pass, RenderGraph::ImageHandle source,
        RenderGraph::ImageHandle filtered, RenderGraph::ImageHandle target
    );

    Layout computeLayout;
    Layout graphicsLayout;
    ComputePipeline downsample;
    ComputePipeline upsample;
    ComputePipeline postFx;
    vk::raii::Sampler borderSampler = nullptr;
    vk::raii::Sampler linearSampler = nullptr;
    vk::raii::Sampler pointSampler = nullptr;
    bool encodeSrgb = false;
    std::vector<std::unique_ptr<ImagePass>> imagePasses;
};
