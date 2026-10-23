#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "render/feature/RenderGraph.h"
#include "render/feature/shadow/ShadowPass.h"

#include <memory>
#include <optional>
#include <vulkan/vulkan_raii.hpp>

class RenderResourceManager;
class GpuTexture;
class VulkanContext;
class RenderGraph;
class FrameData;
struct RenderData;

class SceneBindings
{
public:
    ShadowPass::Output setup(RenderGraph& graph, const PassSetup& setup, ShadowPass::Output shadow);
    void               refreshBindings(const RendererConfig& config);
    void               writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const;
    void               updateFrame(FrameData& frame, const RenderData& scene, const RendererConfig& config) const;
    void               updateEnvironmentMap(const std::optional<EnvironmentMapAsset::ID>& id);

private:
    std::optional<EnvironmentMapAsset::ID> environmentMap;
    VulkanContext*                         vulkan = nullptr;
    std::span<FrameData>                   frameData;
    RenderGraph::ImageHandle               pointShadowMap;
    RenderGraph::ImageHandle               spotShadowMap;
    RenderGraph::ImageHandle               rectAreaShadowMap;
    RenderGraph::ImageHandle               cascadeShadowMap;
    RenderResourceManager*                 resources = nullptr;
    std::shared_ptr<GpuTexture>            irradianceTexture;
    std::shared_ptr<GpuTexture>            prefilteredSpecularTexture;
    std::shared_ptr<GpuTexture>            brdfLutTexture;
    std::shared_ptr<GpuTexture>            sheenLutTexture;
    std::shared_ptr<GpuTexture>            radianceTexture;
    vk::raii::Sampler                      shadowSampler     = nullptr;
    vk::Filter                             shadowFilter      = vk::Filter::eLinear;
    float                                  prefilteredMaxLod = 0.0f;

    void bindEnvironmentTextures();
};
