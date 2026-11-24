#pragma once

#include "render/graph/RenderPassSettings.h"

#include "passes/postprocess/ToneMappingPass/ToneMappingSettings.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

class ToneMappingPass : public RenderPass
{
public:
    ToneMappingPass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties);

    static std::shared_ptr<ToneMappingPass> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<ToneMappingPass>(std::move(device), properties);
    }

    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    ToneMappingSettings parameters;

    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    bool                              encodeSrgb = false;

    static const RenderPassReflection::Field kPorts[3];

    RL_PLUGIN_CLASS(
        ToneMappingPass,
        "ToneMappingPass",
        .desc = "Applies tone mapping and exposure.",
        .ports = kPorts,
        .settings = pass_settings::bind<&ToneMappingPass::parameters>()
    );
};
