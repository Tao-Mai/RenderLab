#pragma once

#include "render/graph/RenderPass.h"

#include <slang-rhi.h>

#include <slang-rhi/shader-cursor.h>

class SkyboxPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(SkyboxPass, "SkyboxPass", "Renders the environment skybox.");

    SkyboxPass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
        Falcor::RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static Falcor::ref<SkyboxPass> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<SkyboxPass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
};
