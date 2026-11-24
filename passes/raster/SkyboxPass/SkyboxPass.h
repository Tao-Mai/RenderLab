#pragma once

#include "render/graph/RenderPass.h"

#include <slang-rhi.h>

#include <slang-rhi/shader-cursor.h>

class SkyboxPass : public RenderPass
{
    RL_PLUGIN_CLASS(SkyboxPass, "SkyboxPass", "Renders the environment skybox.");

    SkyboxPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
        RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static ref<SkyboxPass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<SkyboxPass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
};
