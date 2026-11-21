#pragma once

#include "render/device/ResourceBarriers.h"
#include "render/graph/RenderPass.h"

#include "passes/raster/ForwardPbr.h"
#include "render/raster/RasterAttachments.h"

#include <vector>

class TransparentPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(TransparentPass, "TransparentPass", "Renders transparent geometry.");

    TransparentPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) : Falcor::RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static Falcor::ref<TransparentPass> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<TransparentPass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    ResourceBarriers barriers;
    void sortDrawItems(const Falcor::RenderContext& frame);

    std::vector<uint32_t> sortedDrawItems;
    RasterAttachments<1>  attachments;
    ForwardPbrPipelines   pipelines;
};
