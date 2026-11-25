#pragma once

#include "render/graph/RenderPass.h"

#include <slang-rhi.h>

#include <slang-rhi/shader-cursor.h>

class SkyboxPass : public RenderPass
{
    static const RenderPassReflection::Field kPorts[2];

    RL_PLUGIN_CLASS(SkyboxPass, "SkyboxPass", .desc = "Renders the environment skybox.", .ports = kPorts);

    SkyboxPass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties) :
        RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static std::shared_ptr<SkyboxPass> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<SkyboxPass>(std::move(device), properties);
    }

    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
};
