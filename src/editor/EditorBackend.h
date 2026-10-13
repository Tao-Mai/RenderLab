#pragma once
#include <vulkan/vulkan.h>
#include "core/profiling/Profiler.h"
struct GLFWwindow;
class EditorBackend
{
public:
    ~EditorBackend();
    void init(
        GLFWwindow*      window,
        VkInstance       instance,
        VkPhysicalDevice physicalDevice,
        VkDevice         device,
        uint32_t         queueFamily,
        VkQueue          queue,
        VkFormat         colorFormat,
        uint32_t         minImageCount,
        uint32_t         imageCount
    );

    void beginFrame();
    [[nodiscard]] DrawStatistics render(VkCommandBuffer commands) const;
    void shutdown() noexcept;

private:
    bool                             contextCreated           = false;
    bool                             glfwBackendInitialized   = false;
    bool                             vulkanBackendInitialized = false;
    VkFormat                         colorFormat              = VK_FORMAT_UNDEFINED;
    VkPipelineRenderingCreateInfoKHR pipelineRenderingInfo{};
};
