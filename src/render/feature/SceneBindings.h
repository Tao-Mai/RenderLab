#pragma once

#include "render/feature/RenderGraph.h"
#include "render/feature/shadow/ShadowPass.h"
#include "asset/EnvironmentMapAsset.h"

#include <memory>
#include <optional>
#include <utility>
#include <vulkan/vulkan_raii.hpp>

class RenderResourceManager;
class GpuTexture;
class VulkanContext;
class RenderGraph;
class FrameData;
struct RenderData;
struct ViewUniforms;

class SceneBindings
{
public:
    ShadowPass::Output setup(RenderGraph& graph, ShadowPass::Output shadow);
    void               refreshBindings();
    void               prepare(RenderGraph& graph) const;
    void               updateFrame(FrameData& frame, const ViewUniforms& view, const RenderData& scene) const;
    void setEnvironmentMap(std::optional<EnvironmentMapAsset::ID> id) { environmentMap = std::move(id); }

private:
    std::optional<EnvironmentMapAsset::ID> environmentMap;
    VulkanContext*              vulkan = nullptr;
    RenderGraph*                graph  = nullptr;
    RenderGraph::ImageHandle    pointShadowMap;
    RenderGraph::ImageHandle    spotShadowMap;
    RenderGraph::ImageHandle    rectAreaShadowMap;
    RenderGraph::ImageHandle    cascadeShadowMap;
    RenderResourceManager*      resources = nullptr;
    std::shared_ptr<GpuTexture> irradianceTexture;
    std::shared_ptr<GpuTexture> prefilteredSpecularTexture;
    std::shared_ptr<GpuTexture> brdfLutTexture;
    vk::Sampler                 brdfLutSampler    = nullptr;
    vk::raii::Sampler           shadowSampler     = nullptr;
    vk::Filter                  shadowFilter      = vk::Filter::eLinear;
    float                       prefilteredMaxLod = 0.0f;

    void bindEnvironmentTextures();
};
