#pragma once
#include <slang-rhi.h>

#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "passes/raster/ForwardPbr.h"
#include "render/raster/RasterLightingSettings.h"

class ScenePass : public RenderPass
{
    RL_PLUGIN_CLASS(ScenePass, "ScenePass", "Renders opaque geometry with forward shading.");

    ScenePass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties);

    [[nodiscard]] Properties getProperties() const override;
    void                             setProperties(const Properties& properties) override;

    static ref<ScenePass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<ScenePass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    RasterLightingOutputSettings parameters;


    ForwardPbrPipelines pipelines;
};
