#pragma once

#include "render/device/ResourceBarriers.h"
#include "render/graph/RenderPass.h"

#include "render/device/Device.h"

#include "render/raster/RasterAttachments.h"
#include "render/raster/RasterLightingSettings.h"

class DeferredLightingPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(DeferredLightingPass, "DeferredLightingPass", "Shades the G-buffer.");

    DeferredLightingPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<DeferredLightingPass> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<DeferredLightingPass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    RasterLightingSettings parameters;

    ResourceBarriers barriers;
    RasterAttachments<1>     attachments;
    DescriptorSetLayout      descriptorSetLayout;
    vk::raii::DescriptorSet  descriptorSet = nullptr;
    vk::Pipeline             pipeline;
    vk::PipelineLayout       pipelineLayout;
};
