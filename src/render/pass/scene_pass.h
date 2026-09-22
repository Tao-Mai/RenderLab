#pragma once

#include "editor/viewport_rect.h"
#include "render/pass/scene_pipelines.h"
#include "render/resource/buffer.h"
#include "render/scene/gpu_scene.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_raii.hpp>

class FrameContext;
class LightMarkers;
class Mesh;
class RenderResourceManager;
class Swapchain;
class VulkanContext;
struct Light;

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
    void updateScene(
        const glm::mat4& viewProjection,
        const glm::vec3& cameraPosition,
        const Light*     light);
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

    VulkanContext*                                         vulkan   = nullptr;
    Swapchain*                                             swapchain = nullptr;
    FrameContext*                                          frame     = nullptr;
    vk::raii::DescriptorPool                               descriptorPool = nullptr;
    vk::raii::DescriptorSetLayout                          sceneLayout    = nullptr;
    vk::raii::DescriptorSetLayout                          materialLayout = nullptr;
    vk::raii::DescriptorSet                                sceneSet       = nullptr;
    Buffer                                                 sceneBuffer;
    ScenePipelines                                         scenePipelines;

    void                     initDescriptors();
    void                     bindSceneDescriptor(vk::raii::CommandBuffer& commandBuffer) const;
};
