#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class Window;

class VulkanContext
{
public:
    void init(Window& window);
    void reset() noexcept;

    [[nodiscard]] const vk::raii::Instance&       instanceHandle() const { return instance; }
    [[nodiscard]] const vk::raii::PhysicalDevice& physicalDeviceHandle() const { return physicalDevice; }
    [[nodiscard]] const vk::raii::Device&         deviceHandle() const { return device; }
    [[nodiscard]] vk::raii::Queue&                queueHandle() { return queue; }
    [[nodiscard]] vk::SurfaceKHR                  surfaceHandle() const { return *surface; }
    [[nodiscard]] uint32_t                        graphicsQueueFamilyIndex() const { return graphicsQueueFamilyIndex_; }
    [[nodiscard]] const vk::PhysicalDeviceProperties& properties() const { return deviceProperties; }
    [[nodiscard]] const vk::PhysicalDeviceFeatures&   supportedFeatures() const { return deviceFeatures; }
    [[nodiscard]] bool                                supportsRayTracing() const { return rayTracingSupported; }

private:
    Window*                          window = nullptr;
    vk::raii::Context                context;
    vk::raii::Instance               instance                  = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger            = nullptr;
    vk::raii::SurfaceKHR             surface                   = nullptr;
    vk::raii::PhysicalDevice         physicalDevice            = nullptr;
    vk::PhysicalDeviceProperties     deviceProperties;
    vk::PhysicalDeviceFeatures       deviceFeatures;
    bool rayTracingSupported = false;
    vk::raii::Device                 device                    = nullptr;
    uint32_t                         graphicsQueueFamilyIndex_ = ~0u;
    vk::raii::Queue                  queue                     = nullptr;
    const std::vector<const char*>   requiredDeviceExtension{vk::KHRSwapchainExtensionName};

    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    bool isDeviceSuitable(const vk::raii::PhysicalDevice& candidate);
    void pickPhysicalDevice();
    void createLogicalDevice();
    std::vector<const char*> getRequiredInstanceExtensions();
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT      severity,
        vk::DebugUtilsMessageTypeFlagsEXT             type,
        const vk::DebugUtilsMessengerCallbackDataEXT* callbackData,
        void*                                         userData);
};
