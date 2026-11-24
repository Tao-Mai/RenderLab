#pragma once

#include "render/device/GpuResources.h"
#include "render/graph/RenderPass.h"

#include "passes/postprocess/Bloom/BloomSettings.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <vector>

class Bloom : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(Bloom, "Bloom", "Extracts and filters highlights for bloom.");

    Bloom(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void                             setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<Bloom> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<Bloom>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
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
            GpuResources::ImageHandle    source,
            GpuResources::ImageHandle    base,
            GpuResources::ImageHandle    target
        );
        void execute(
            GpuResources& cache, rhi::ICommandEncoder& commandBuffer, Extent2D frameExtent, const ViewportRect& viewport
        ) const;

    private:
        Stage                     stage;
        const BloomSettings&      settings;
        GpuResources::ImageHandle source;

        GpuResources::ImageHandle target;

        rhi::ComPtr<rhi::IShaderObject>   shaderObject;
        rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    };

    std::vector<Filter> filters;
};
