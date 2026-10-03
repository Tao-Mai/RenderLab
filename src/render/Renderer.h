#pragma once

#include "core/Debug.h"

#include "editor/ViewportRect.h"
#include "render/device/FrameContext.h"
#include "render/RenderConfig.h"
#include "render/device/VulkanContext.h"
#include "render/DescriptorManager.h"
#include "render/PipelineManager.h"
#include "render/ShaderManager.h"
#include "render/pass/RenderGraph.h"
#include "render/present/Swapchain.h"
#include "render/resource/RenderResourceManager.h"
#include "render/scene/GpuScene.h"

#include <array>
#include <cstdint>
#include <functional>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

class CameraComponent;

struct EditorFrameInput
{
    ViewportRect viewport;
    float        aspectRatio      = 1.0f;
    bool         requestPick      = false;
    uint32_t     pickX            = 0;
    uint32_t     pickY            = 0;
    AActor* selectedActor = nullptr;
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
    void loadScene();
    EditorFrameResult render(const CameraComponent& camera, const EditorFrameInput& editor);
    void waitIdle();
    void shutdown() noexcept;

    [[nodiscard]] VulkanContext& vulkanContext();
    [[nodiscard]] Swapchain&     swapchainHandle();
    [[nodiscard]] const GpuScene& gpuScene() const;

private:
    friend class RenderGraph;
    DEBUG_ONLY(bool inited = false;)
    VulkanContext     vulkan;
    Swapchain         swapchain;
    ShaderManager     shaders;
    DescriptorManager descriptors;
    PipelineManager   pipelines;
    RenderResourceManager resources;
    std::array<FrameContext, maxFramesInFlight> frames;
    uint32_t          frameIndex = 0;
    GpuScene          scene;
    RenderGraph       graph;
    glm::vec4         iblParameters{0.0f};

    void initVulkan();
    void updateFrameData(const CameraComponent& camera, const EditorFrameInput& editor);
    [[nodiscard]] FrameContext& currentFrame();
    void recreateSwapchain();
    EditorFrameResult drawFrame(const CameraComponent& camera, const EditorFrameInput& editor);
};
