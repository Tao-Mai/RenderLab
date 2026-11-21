#pragma once

#include "render/device/ResourceBarriers.h"
#include "render/graph/RenderPass.h"

#include "passes/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h"
#include "render/graph/RenderPass.h"

#include "render/device/Device.h"
#include "render/raster/RasterAttachments.h"

#include <vector>

// Connects Falcor's SimplePostFX and ToneMapper, followed by swapchain presentation.
class FalcorPostProcess : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(FalcorPostProcess, "FalcorPostProcess", "Applies Falcor tone mapping and post effects.");

    FalcorPostProcess(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<FalcorPostProcess> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<FalcorPostProcess>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    FalcorPostProcessConfig parameters;

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

    class ImageStage
    {
    public:
        ImageStage(const FalcorPostProcess& owner, Stage stage, const ComputePipeline& compute, uint32_t level = 0) :
            owner(owner), layout(owner.computeLayout), stage(stage), compute(&compute), level(level)
        {
        }

        ImageStage(const FalcorPostProcess& owner, Stage stage, ShaderHandle shader) :
            owner(owner), layout(owner.graphicsLayout), stage(stage), shader(shader)
        {
        }

        void compile(
            VulkanResources&             cache,
            VulkanResources::ImageHandle source,
            VulkanResources::ImageHandle filtered,
            VulkanResources::ImageHandle target
        );
        void execute(
            VulkanResources& cache, vk::raii::CommandBuffer& commands, VulkanResources::ImageHandle colorOutput
        );

    private:
        void dispatch(VulkanResources& cache, vk::raii::CommandBuffer& commands) const;

        const FalcorPostProcess& owner;
        const Layout&            layout;
        Stage                    stage;
        const ComputePipeline*   compute = nullptr;
        ShaderHandle             shader;
        uint32_t                 level = 0;
        VulkanResources::ImageHandle storageTarget;
        RasterAttachments<1>     attachments;
        ResourceBarriers       accesses;
        vk::raii::DescriptorSet  descriptorSet = nullptr;
        vk::Pipeline             pipeline;
    };

    void initialize(const Falcor::RenderContext& context);
    VulkanResources::ImageHandle
    addSimplePostFx(VulkanResources& cache, const Falcor::RenderData& data, VulkanResources::ImageHandle source);
    VulkanResources::ImageHandle addToneMapper(
        ShaderManager& shaders, VulkanResources& cache, const Falcor::RenderData& data, VulkanResources::ImageHandle source
    );
    VulkanResources::ImageHandle addImageStage(
        VulkanResources&             cache,
        ImageStage                 pass,
        VulkanResources::ImageHandle source,
        VulkanResources::ImageHandle filtered,
        VulkanResources::ImageHandle target
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
    std::vector<ImageStage> imageStages;
};
