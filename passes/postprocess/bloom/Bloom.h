#pragma once

#include "render/ViewportRect.h"
#include "render/graph/RenderPassSettings.h"

#include "passes/postprocess/Bloom/BloomSettings.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <vector>

class Bloom : public RenderPass
{
public:
    Bloom(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties);

    static std::shared_ptr<Bloom> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<Bloom>(std::move(device), properties);
    }

    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    BloomSettings parameters;

    void onSettingsUpdated(const BloomSettings& previous);

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

        void compile(
            rhi::IDevice& device, const RenderContext& context,
            rhi::ITexture* source, rhi::ITexture* base, rhi::ITexture* target
        );
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

    static bool drawSettings(
        Gui::Widgets& widgets, BloomSettings& settings, const Bloom& pass, const PassEditorContext& context
    );

    static const RenderPassReflection::Field kPorts[2];

    RL_PLUGIN_CLASS(
        Bloom,
        "Bloom",
        .desc = "Extracts and filters highlights for bloom.",
        .ports = kPorts,
        .settings = pass_settings::bind<&Bloom::parameters, &Bloom::drawSettings, &Bloom::onSettingsUpdated>()
    );
};
