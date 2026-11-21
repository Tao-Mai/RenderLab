#pragma once

#include "render/device/ResourceBarriers.h"
#include "render/graph/RenderPass.h"

#include "passes/postprocess/Bloom/BloomSettings.h"
#include "render/graph/RenderPass.h"

#include "render/device/Device.h"
#include "render/raster/RasterAttachments.h"

#include <vector>

class Bloom : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(Bloom, "Bloom", "Extracts and filters highlights for bloom.");

    Bloom(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<Bloom> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<Bloom>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    BloomSettings parameters;

    void prepareFilters(const Falcor::RenderContext& context, const Falcor::RenderData& renderData);

    class Filter
    {
    public:
        enum class Stage : uint32_t
        {
            DownsampleHorizontal,
            DownsampleVertical,
            Upsample,
            Composite
        };

        Filter(Stage stage, const BloomSettings& settings) : stage(stage), settings(settings) {}

        void compile(
            const Falcor::RenderContext& context,
            VulkanResources::ImageHandle source,
            VulkanResources::ImageHandle base,
            VulkanResources::ImageHandle target
        );
        void execute(VulkanResources& cache, vk::raii::CommandBuffer& commandBuffer, vk::Extent2D frameExtent, const ViewportRect& viewport) const;

    private:
        Stage                    stage;
        const BloomSettings&     settings;
        VulkanResources::ImageHandle source;
        RasterAttachments<1>     attachments;
        ResourceBarriers       accesses;
        DescriptorSetLayout      descriptorSetLayout;
        vk::raii::DescriptorSet  descriptorSet = nullptr;
        vk::Pipeline             pipeline;
        vk::PipelineLayout       pipelineLayout;
    };

    std::vector<Filter> filters;
};
