#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

struct GLFWwindow;

class EditorUI
{
  public:
    EditorUI() = default;
    ~EditorUI();

    EditorUI(const EditorUI &)            = delete;
    EditorUI &operator=(const EditorUI &) = delete;

    void initialize(
        GLFWwindow *window,
        VkInstance instance,
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        uint32_t queueFamily,
        VkQueue queue,
        VkFormat colorFormat,
        uint32_t minImageCount,
        uint32_t imageCount);
    void beginFrame(float deltaTime);
    void render(VkCommandBuffer commandBuffer) const;
    void shutdown() noexcept;

  private:
    bool contextCreated = false;
    bool glfwBackendInitialized = false;
    bool vulkanBackendInitialized = false;
    bool dockLayoutInitialized = false;
    float fpsRefreshTime = 0.0f;
    float displayedFps = 0.0f;
    uint32_t framesSinceRefresh = 0;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkPipelineRenderingCreateInfoKHR pipelineRenderingInfo{};
};
