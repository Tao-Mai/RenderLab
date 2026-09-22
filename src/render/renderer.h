#pragma once

#include "editor/viewport_rect.h"
#include "render/device/frame_context.h"
#include "render/device/vulkan_context.h"
#include "render/pass/editor_picking_pass.h"
#include "render/pass/scene_pass.h"
#include "render/present/swapchain.h"
#include "render/scene/gpu_scene.h"

#include <cstdint>
#include <functional>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

class AssetManager;
class Camera;
class Scene;
class Window;
struct Light;
struct SceneObject;

struct EditorFrameInput
{
    ViewportRect viewport;
    float        aspectRatio      = 1.0f;
    bool         requestPick      = false;
    uint32_t     pickX            = 0;
    uint32_t     pickY            = 0;
    SceneObject* selectedObject   = nullptr;
    Light*       selectedLight    = nullptr;
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

    void initialize(Window& window);
    void loadScene(Scene& scene, AssetManager& assets);
    EditorFrameResult render(const Camera& camera, const EditorFrameInput& editor);
    void waitIdle();
    void shutdown() noexcept;

    [[nodiscard]] VulkanContext& vulkanContext();
    [[nodiscard]] Swapchain&     swapchainHandle();
    [[nodiscard]] const GpuScene& gpuScene() const;

private:
    bool              initialized = false;
    Window*           window      = nullptr;
    VulkanContext     vulkan;
    Swapchain         swapchain;
    FrameContext      frame;
    GpuScene          scene;
    ScenePass         scenePass;
    EditorPickingPass pickingPass;

    void initVulkan();
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
