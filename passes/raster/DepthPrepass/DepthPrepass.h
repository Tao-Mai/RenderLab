#pragma once

#include "render/device/ResourceBarriers.h"
#include "render/graph/RenderPass.h"

#include "render/raster/MeshDraw.h"
#include "render/raster/RasterAttachments.h"

class DepthPrepass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(DepthPrepass, "DepthPrepass", "Writes the main view depth buffer.");

    DepthPrepass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) : Falcor::RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static Falcor::ref<DepthPrepass> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<DepthPrepass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    ResourceBarriers barriers;
    RasterAttachments<0> attachments;
    MeshCullPipelines    pipelines;
};
