#pragma once

#include "render/device/ResourceBarriers.h"
#include "ExposurePassSettings.h"
#include "render/graph/RenderPass.h"

#include "asset/Asset.h"
#include "core/Logger.h"
#include "render/postprocess/ExposureState.h"
#include "render/device/Device.h"
#include "render/device/VulkanResources.h"

class ExposurePass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(ExposurePass, kExposurePassType, "Computes exposure from a luminance histogram.");

    ExposurePass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<ExposurePass> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<ExposurePass>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* context, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    ExposurePassSettings parameters;

    ResourceBarriers barriers;
    VulkanResources::BufferHandle       histogram;
    DescriptorSetLayout               descriptorSetLayout;
    vk::PipelineLayout                pipelineLayout;
    ComputePipeline                   histogramPipeline;
    ComputePipeline                   resolvePipeline;
    vk::raii::DescriptorSet           descriptorSet = nullptr;
    vk::Extent3D                      extent;
    AssetID<struct SceneAsset>        previousScene;
    bool                              hasHistory = false;
};
