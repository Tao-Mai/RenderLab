#pragma once

#include "render/device/ResourceBarriers.h"
#include "render/graph/RenderPass.h"

#include "render/device/Device.h"

#include "render/raster/RasterAttachments.h"

class SkyboxPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(SkyboxPass, "SkyboxPass", "Renders the environment skybox.");

    SkyboxPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) : Falcor::RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static Falcor::ref<SkyboxPass> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<SkyboxPass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    ResourceBarriers barriers;
    RasterAttachments<1> attachments;
    vk::Pipeline             pipeline;
    vk::PipelineLayout           pipelineLayout;
};
