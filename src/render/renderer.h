#pragma once

#include "render/mesh.h"
#include "window.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_raii.hpp>

class AssetManager;
class Scene;

class Renderer
{
  public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer &)            = delete;
    Renderer &operator=(const Renderer &) = delete;

    void initialize(Window &window);
    void loadScene(const Scene &scene, AssetManager &assets);
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
    vk::Format                       depthFormat = vk::Format::eUndefined;
    vk::raii::DeviceMemory           depthImageMemory = nullptr;
    vk::raii::Image                  depthImage = nullptr;
    vk::raii::ImageView              depthImageView = nullptr;
    vk::raii::PipelineLayout         pipelineLayout   = nullptr;
    vk::raii::Pipeline               graphicsPipeline = nullptr;
    vk::raii::CommandPool            commandPool      = nullptr;
    vk::raii::CommandBuffer          commandBuffer    = nullptr;
    vk::raii::Semaphore              presentCompleteSemaphore = nullptr;
    vk::raii::Semaphore              renderFinishedSemaphore  = nullptr;
    vk::raii::Fence                  drawFence                = nullptr;
    std::vector<const char *>         requiredDeviceExtension{vk::KHRSwapchainExtensionName};

    struct RenderItem
    {
        Mesh *mesh = nullptr;
        glm::mat4 model{1.0f};
    };
    std::unordered_map<std::string, std::unique_ptr<Mesh>> meshAssets;
    std::vector<RenderItem> renderItems;
    glm::mat4 viewProjection{1.0f};

    void initVulkan();
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    bool isDeviceSuitable(const vk::raii::PhysicalDevice &candidate);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createDepthResources();
    void createGraphicsPipeline();
    void createCommandPool();
    void transitionDepthImageLayout();
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

    static uint32_t chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR &surfaceCapabilities);
    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats);
    static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes);
    vk::Format chooseDepthFormat() const;
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities);
    std::vector<const char *> getRequiredInstanceExtensions();
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT *callbackData,
        void *userData);
};
