#pragma once

#include "render/Image.h"

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;
class Window;

class Swapchain
{
public:
    void                                    init(VulkanContext& vulkan, Window& window);
    void                                    reset() noexcept;
    [[nodiscard]] vk::ResultValue<uint32_t> acquire(vk::Semaphore imageAvailable) const;
    [[nodiscard]] vk::Result                present(uint32_t imageIndex) const;

    [[nodiscard]] const vk::raii::SwapchainKHR& handle() const { return swapChain; }
    [[nodiscard]] Image&                        image(uint32_t index) { return swapChainImages[index]; }
    [[nodiscard]] vk::Semaphore                 renderFinishedSemaphore(uint32_t index) const;
    [[nodiscard]] vk::SurfaceFormatKHR          surfaceFormat() const { return swapChainSurfaceFormat; }
    [[nodiscard]] vk::Extent2D                  extent() const { return swapChainExtent; }
    [[nodiscard]] uint32_t                      minImageCount() const { return swapChainMinImageCount; }
    [[nodiscard]] uint32_t imageCount() const { return static_cast<uint32_t>(swapChainImages.size()); }

private:
    VulkanContext*                   vulkan    = nullptr;
    Window*                          window    = nullptr;
    vk::raii::SwapchainKHR           swapChain = nullptr;
    std::vector<Image>               swapChainImages;
    vk::SurfaceFormatKHR             swapChainSurfaceFormat;
    vk::Extent2D                     swapChainExtent;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    uint32_t                         swapChainMinImageCount = 0;

    void                        createSwapChain();
    static uint32_t             chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& capabilities);
    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
    static vk::PresentModeKHR   chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& modes);
    vk::Extent2D                chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);
};
