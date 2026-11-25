#pragma once

#include "render/graph/RenderPass.h"

#include "render/impl/postprocess/ToneMappingPass/ToneMappingSettings.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

class ToneMappingPass final : public RenderPass
{
public:
    static constexpr std::string_view kType = "ToneMappingPass";

    explicit ToneMappingPass(rhi::ComPtr<rhi::IDevice> device);

    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    static bool drawSettings(
        Gui::Widgets& widgets, ToneMappingSettings& settings, const ToneMappingPass& pass, const PassEditorContext& context
    );

    ToneMappingSettings parameters;

    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    bool                              encodeSrgb = false;

    static const RenderPassReflection::Field kPorts[3];
};
