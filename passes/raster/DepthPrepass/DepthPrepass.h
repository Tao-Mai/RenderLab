#pragma once
#include <slang-rhi.h>

#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "render/raster/MeshDraw.h"

class DepthPrepass : public RenderPass
{
    RL_PLUGIN_CLASS(DepthPrepass, "DepthPrepass", "Writes the main view depth buffer.");

    DepthPrepass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) : RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static ref<DepthPrepass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<DepthPrepass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    MeshCullPipelines pipelines;
};
