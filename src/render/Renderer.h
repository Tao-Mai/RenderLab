#pragma once

#include "editor/ViewportRect.h"
#include "render/device/FrameContext.h"
#include "render/device/VulkanContext.h"
#include "render/pass/EditorPickingPass.h"
#include "render/pass/ScenePass.h"
#include "render/present/Swapchain.h"
#include "render/resource/RenderResourceManager.h"
#include "render/scene/GpuScene.h"

#include <cstdint>
#include <functional>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

class Camera;
struct SceneDesc;
struct SceneObjectDesc;

struct EditorFrameInput
{
    ViewportRect viewport;
    float        aspectRatio      = 1.0f;
    bool         requestPick      = false;
    uint32_t     pickX            = 0;
    uint32_t     pickY            = 0;
    SceneObjectDesc* selectedObject = nullptr;
    std::function<void(VkCommandBuffer)> recordUi;
};

struct EditorFrameResult
{
    uint32_t pickedSelectionId = 0;
    bool     hasPickResult     = false;
};

class Renderer
{
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    void init();
    void loadScene(SceneDesc& scene);
    EditorFrameResult render(const Camera& camera, const EditorFrameInput& editor);
    void waitIdle();
    void shutdown() noexcept;

    [[nodiscard]] VulkanContext& vulkanContext();
    [[nodiscard]] Swapchain&     swapchainHandle();
    [[nodiscard]] const GpuScene& gpuScene() const;

private:
    bool              inited = false;
    VulkanContext     vulkan;
    Swapchain         swapchain;
    FrameContext      frame;
    RenderResourceManager resources;
    GpuScene          scene;
    ScenePass         scenePass;
    EditorPickingPass pickingPass;

    void initVulkan();
    void recreateSwapchain();
    void recordCommandBuffer(uint32_t imageIndex, const EditorFrameInput& editor);
    void recordPickingPass(vk::raii::CommandBuffer& commandBuffer, const EditorFrameInput& editor);
    void recordUiPass(
        vk::raii::CommandBuffer& commandBuffer,
        uint32_t                 imageIndex,
        const EditorFrameInput&  editor);
    void transitionImageLayout(
        uint32_t                imageIndex,
        vk::ImageLayout         oldLayout,
        vk::ImageLayout         newLayout,
        vk::AccessFlags2        srcAccessMask,
        vk::AccessFlags2        dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask,
        vk::PipelineStageFlags2 dstStageMask);
    EditorFrameResult drawFrame(const EditorFrameInput& editor);
};
