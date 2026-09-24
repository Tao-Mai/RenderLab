#pragma once

#include "render/RenderConfig.h"

#include <array>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
class Window;

class Swapchain
{
public:
    void init(VulkanContext& vulkan, Window& window);
    void transitionDepthImageLayout(const vk::raii::CommandPool& commandPool);
    void reset() noexcept;

    [[nodiscard]] const vk::raii::SwapchainKHR& handle() const;
    [[nodiscard]] vk::Image                     image(uint32_t index) const;
    [[nodiscard]] vk::ImageView                 imageView(uint32_t index) const;
    [[nodiscard]] vk::Semaphore                 renderFinishedSemaphore(uint32_t index) const;
    [[nodiscard]] vk::Image                     depthImageHandle(uint32_t frameIndex) const;
    [[nodiscard]] vk::ImageView                 depthImageViewHandle(uint32_t frameIndex) const;
    [[nodiscard]] vk::Image                     pickingImageHandle() const;
    [[nodiscard]] vk::ImageView                 pickingImageViewHandle() const;
    [[nodiscard]] vk::SurfaceFormatKHR          surfaceFormat() const;
    [[nodiscard]] vk::Extent2D                  extent() const;
    [[nodiscard]] vk::Format                    depthImageFormat() const;
    [[nodiscard]] uint32_t                      minImageCount() const;
    [[nodiscard]] uint32_t                      imageCount() const;

private:
    VulkanContext*                   vulkan    = nullptr;
    Window*                          window    = nullptr;
    vk::raii::SwapchainKHR           swapChain = nullptr;
    std::vector<vk::Image>           swapChainImages;
    vk::SurfaceFormatKHR             swapChainSurfaceFormat;
    vk::Extent2D                     swapChainExtent;
    std::vector<vk::raii::ImageView> swapChainImageViews;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    vk::Format                       depthFormat            = vk::Format::eUndefined;
    struct DepthTarget
    {
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::Image image = nullptr;
        vk::raii::ImageView view = nullptr;
    };
    std::array<DepthTarget, maxFramesInFlight> depthTargets;
    vk::raii::DeviceMemory           pickingImageMemory     = nullptr;
    vk::raii::Image                  pickingImage           = nullptr;
    vk::raii::ImageView              pickingImageView       = nullptr;
    uint32_t                         swapChainMinImageCount = 0;

    void            createSwapChain();
    void            createImageViews();
    void            createDepthResources();
    void            createPickingResources();
    static uint32_t chooseSwapMinImageCount(
        const vk::SurfaceCapabilitiesKHR& capabilities);
    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<vk::SurfaceFormatKHR>& formats);
    static vk::PresentModeKHR chooseSwapPresentMode(
        const std::vector<vk::PresentModeKHR>& modes);
    [[nodiscard]] vk::Format chooseDepthFormat() const;
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);
};
