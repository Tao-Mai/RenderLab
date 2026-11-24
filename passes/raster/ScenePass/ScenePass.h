#pragma once
#include <slang-rhi.h>

#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "passes/raster/ForwardPbr.h"
#include "render/raster/RasterLightingSettings.h"

class ScenePass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(ScenePass, "ScenePass", "Renders opaque geometry with forward shading.");

    ScenePass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void                             setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<ScenePass> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<ScenePass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    RasterLightingSettings parameters;


    ForwardPbrPipelines pipelines;
};
