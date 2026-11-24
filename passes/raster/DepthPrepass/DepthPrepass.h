#pragma once
#include <slang-rhi.h>

#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "render/raster/MeshDraw.h"

class DepthPrepass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(DepthPrepass, "DepthPrepass", "Writes the main view depth buffer.");

    DepthPrepass(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
        Falcor::RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static Falcor::ref<DepthPrepass> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<DepthPrepass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    MeshCullPipelines pipelines;
};
