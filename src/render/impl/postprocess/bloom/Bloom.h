#pragma once

#include "render/graph/RenderPass.h"

#include "render/impl/postprocess/bloom/BloomSettings.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <vector>

class Bloom final : public RenderPass
{
public:
    static constexpr std::string_view kType = "Bloom";

    explicit Bloom(rhi::ComPtr<rhi::IDevice> device);

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
        void execute(rhi::ICommandEncoder& commands) const;

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
};
