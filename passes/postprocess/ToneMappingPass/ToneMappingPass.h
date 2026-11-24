#pragma once

#include "render/graph/RenderPass.h"

#include "passes/postprocess/ToneMappingPass/ToneMappingSettings.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

class ToneMappingPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(ToneMappingPass, "ToneMappingPass", "Applies tone mapping and exposure.");

    ToneMappingPass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void                             setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<ToneMappingPass> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<ToneMappingPass>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    ToneMappingSettings parameters;

    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    bool                              encodeSrgb = false;
};
