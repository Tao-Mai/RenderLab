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

    [[nodiscard]] const vk::raii::Instance&       instanceHandle() const;
    [[nodiscard]] const vk::raii::PhysicalDevice& physicalDeviceHandle() const;
    [[nodiscard]] const vk::raii::Device&         deviceHandle() const;
    [[nodiscard]] vk::raii::Queue&                queueHandle();
    [[nodiscard]] vk::SurfaceKHR                  surfaceHandle() const;
    [[nodiscard]] uint32_t                        graphicsQueueFamilyIndex() const;

private:
    Window*                          window = nullptr;
    vk::raii::Context                context;
    vk::raii::Instance               instance                  = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger            = nullptr;
    vk::raii::SurfaceKHR             surface                   = nullptr;
    vk::raii::PhysicalDevice         physicalDevice            = nullptr;
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
