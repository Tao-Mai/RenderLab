#pragma once

#include "render/ViewportRect.h"
#include "render/graph/RenderPass.h"

#include "passes/postprocess/Bloom/BloomSettings.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <vector>

class Bloom : public RenderPass
{
    RL_PLUGIN_CLASS(Bloom, "Bloom", "Extracts and filters highlights for bloom.");

    Bloom(rhi::ComPtr<rhi::IDevice> device, const Properties& properties);

    [[nodiscard]] Properties getProperties() const override;
    void                             setProperties(const Properties& properties) override;

    static ref<Bloom> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<Bloom>(std::move(device), properties);
    }

    void renderUI(const PassEditorContext& context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    BloomSettings parameters;

    void prepareFilters(const RenderContext& context, const RenderData& renderData);

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

        void compile(const RenderContext& context, rhi::ITexture* source, rhi::ITexture* base, rhi::ITexture* target);
        void execute(rhi::ICommandEncoder& commands, Extent2D frameExtent, const ViewportRect& viewport) const;

    private:
        Stage                     stage;
        const BloomSettings&      settings;
        glm::vec2                 sourceTexelSize;

        rhi::ComPtr<rhi::ITexture>        target;
        rhi::ComPtr<rhi::IShaderObject>   shaderObject;
        rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    };

    std::vector<Filter> filters;
};
