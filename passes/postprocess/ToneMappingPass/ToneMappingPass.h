#pragma once

#include "render/device/ResourceBarriers.h"
#include "render/graph/RenderPass.h"

#include "passes/postprocess/ToneMappingPass/ToneMappingSettings.h"
#include "render/graph/RenderPass.h"

#include "render/device/Device.h"
#include "render/raster/RasterAttachments.h"

class ToneMappingPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(ToneMappingPass, "ToneMappingPass", "Applies tone mapping and exposure.");

    ToneMappingPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<ToneMappingPass> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<ToneMappingPass>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    ToneMappingSettings parameters;

    ResourceBarriers barriers;
    RasterAttachments<1>      attachments;
    DescriptorSetLayout       descriptorSetLayout;
    vk::raii::DescriptorSet   descriptorSet = nullptr;
    vk::Pipeline              pipeline;
    vk::PipelineLayout        pipelineLayout;
    bool                      encodeSrgb = false;
};
