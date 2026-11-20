#pragma once

#include "render/graph/RenderPass.h"

#include "asset/Asset.h"
#include "core/Logger.h"
#include "render/ExposureConfig.h"
#include "render/postprocess/ExposureState.h"
#include "render/graph/PassSettings.h"
#include "render/device/Device.h"
#include "render/graph/RenderGraph.h"

#include <array>

struct ExposurePassSettings : ExposureConfig
{
    void validate() const { CHECK(isValid()); }
};

class ExposurePass : public ConfigurablePass<ExposurePassSettings>, public ExposureProvider
{
public:
    [[nodiscard]] const ExposureConfig& exposureDefaults() const override { return parameters; }


    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    void   bindResources(RenderGraph& graph, const PassSetup& setup) override;
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame) override;

private:
    RenderGraph::ImageHandle          source;
    RenderGraph::BufferHandle         histogram;
    RenderGraph::BufferHandle                            exposureState;
    DescriptorSetLayout               descriptorSetLayout;
    vk::PipelineLayout                pipelineLayout;
    std::array<vk::raii::Pipeline, 2> pipelines{nullptr, nullptr};
    vk::raii::DescriptorSet           descriptorSet = nullptr;
    vk::Extent3D                      extent;
    AssetID<struct SceneAsset>        previousScene;
    bool                              hasHistory = false;
};
