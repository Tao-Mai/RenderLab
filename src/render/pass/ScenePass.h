#pragma once

#include "editor/ViewportRect.h"
#include "render/pass/ScenePipelines.h"
#include "render/resource/Buffer.h"
#include "render/scene/GpuScene.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_raii.hpp>

class FrameContext;
class LightMarkers;
class Mesh;
class RenderResourceManager;
class Swapchain;
class Texture;
class VulkanContext;
struct SceneDesc;
struct SceneObjectDesc;

class ScenePass
{
public:
    struct Target
    {
        uint32_t     imageIndex;
        ViewportRect viewport;
        bool         storeDepthForPicking;
    };

    void init(VulkanContext& vulkan, Swapchain& swapchain, FrameContext& frame, RenderResourceManager& resources);
    void reset() noexcept;
    void bindEnvironment(const SceneDesc& scene);
    void updateScene(
        const glm::mat4& viewProjection,
        const glm::vec3& cameraPosition,
        const SceneObjectDesc* lightObject);
    void record(
        vk::raii::CommandBuffer&            commandBuffer,
        const std::vector<SceneRenderItem>& renderItems,
        const std::vector<LightRenderItem>& lightRenderItems,
        const LightMarkers&                 lightMarkers,
        Target                              target) const;

    [[nodiscard]] vk::DescriptorSetLayout sceneLayoutHandle() const;
    [[nodiscard]] vk::DescriptorSet       sceneSetHandle() const;

private:
    static constexpr uint32_t sceneSetIndex    = 0;
    static constexpr uint32_t materialSetIndex = 1;

    VulkanContext*                                         vulkan    = nullptr;
    Swapchain*                                             swapchain = nullptr;
    FrameContext*                                          frame     = nullptr;
    RenderResourceManager*                                 resources = nullptr;
    vk::raii::DescriptorPool                               descriptorPool = nullptr;
    vk::raii::DescriptorSetLayout                          sceneLayout    = nullptr;
    vk::raii::DescriptorSetLayout                          materialLayout = nullptr;
    vk::raii::DescriptorSet                                sceneSet       = nullptr;
    Buffer                                                 sceneBuffer;
    std::shared_ptr<Texture>                              environmentTexture;
    ScenePipelines                                         scenePipelines;

    void                     initDescriptors();
    void                     bindSceneDescriptor(vk::raii::CommandBuffer& commandBuffer) const;
};
