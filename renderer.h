#pragma once

#include "window.h"

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class Renderer
{
  public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer &)            = delete;
    Renderer &operator=(const Renderer &) = delete;

    void initialize(Window &window);
    void render();
    void waitIdle();
    void shutdown() noexcept;

  private:
    bool                             initialized = false;
    Window                          *window      = nullptr;
    vk::raii::Context                context;
    vk::raii::Instance               instance       = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::SurfaceKHR             surface        = nullptr;
    vk::raii::PhysicalDevice         physicalDevice = nullptr;
    vk::raii::Device                 device         = nullptr;
    uint32_t                         queueIndex      = ~0u;
    vk::raii::Queue                  queue           = nullptr;
    vk::raii::SwapchainKHR           swapChain       = nullptr;
    std::vector<vk::Image>           swapChainImages;
    vk::SurfaceFormatKHR             swapChainSurfaceFormat;
    vk::Extent2D                     swapChainExtent;
    std::vector<vk::raii::ImageView> swapChainImageViews;
    vk::raii::PipelineLayout         pipelineLayout   = nullptr;
    vk::raii::Pipeline               graphicsPipeline = nullptr;
    vk::raii::CommandPool            commandPool      = nullptr;
    vk::raii::CommandBuffer          commandBuffer    = nullptr;
    vk::raii::Semaphore              presentCompleteSemaphore = nullptr;
    vk::raii::Semaphore              renderFinishedSemaphore  = nullptr;
    vk::raii::Fence                  drawFence                = nullptr;
    std::vector<const char *>         requiredDeviceExtension{vk::KHRSwapchainExtensionName};

    void initVulkan();
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    bool isDeviceSuitable(const vk::raii::PhysicalDevice &candidate);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createGraphicsPipeline();
    void createCommandPool();
    void createCommandBuffer();
    void recordCommandBuffer(uint32_t imageIndex);
    void transition_image_layout(
        uint32_t imageIndex,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags2 srcAccessMask,
        vk::AccessFlags2 dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask,
        vk::PipelineStageFlags2 dstStageMask);
    void createSyncObjects();
    void drawFrame();

    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char> &code) const;
    static uint32_t chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR &surfaceCapabilities);
    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats);
    static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes);
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities);
    std::vector<const char *> getRequiredInstanceExtensions();
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT *callbackData,
        void *userData);
    static std::vector<char> readFile(const std::string &filename);
};
