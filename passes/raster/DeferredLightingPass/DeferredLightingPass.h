#pragma once

#include "render/graph/RenderPass.h"

#include <slang-rhi.h>

#include <slang-rhi/shader-cursor.h>
#include "render/raster/RasterLightingSettings.h"

class DeferredLightingPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(DeferredLightingPass, "DeferredLightingPass", "Shades the G-buffer.");

    DeferredLightingPass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void                             setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<DeferredLightingPass>
    create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<DeferredLightingPass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    RasterLightingSettings parameters;

    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
};
