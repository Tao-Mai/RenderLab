#pragma once

#include "render/graph/RenderPass.h"

#include "passes/postprocess/ToneMappingPass/ToneMappingSettings.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

class ToneMappingPass : public RenderPass
{
    RL_PLUGIN_CLASS(ToneMappingPass, "ToneMappingPass", "Applies tone mapping and exposure.");

    ToneMappingPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties);

    [[nodiscard]] Properties getProperties() const override;
    void                             setProperties(const Properties& properties) override;

    static ref<ToneMappingPass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<ToneMappingPass>(std::move(device), properties);
    }

    void renderUI(const PassEditorContext& context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    ToneMappingSettings parameters;

    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    bool                              encodeSrgb = false;
};
