#include "render/present/Swapchain.h"

#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "core/Window.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include "core/Logger.h"

void Swapchain::init(VulkanContext& context, Window& targetWindow)
{
    vulkan = &context;
    window = &targetWindow;
    createSwapChain();
    createImageViews();
}

void Swapchain::reset() noexcept
{
    swapChainImageViews.clear();
    renderFinishedSemaphores.clear();
    swapChainImages.clear();
    swapChain              = nullptr;
    swapChainMinImageCount = 0;
    window                 = nullptr;
    vulkan                 = nullptr;
}

const vk::raii::SwapchainKHR& Swapchain::handle() const { return swapChain; }
vk::Image Swapchain::image(uint32_t index) const { return swapChainImages[index]; }

vk::ImageView Swapchain::imageView(uint32_t index) const
{
    return *swapChainImageViews[index];
}

vk::Semaphore Swapchain::renderFinishedSemaphore(uint32_t index) const
{
    return *renderFinishedSemaphores.at(index);
}

vk::SurfaceFormatKHR Swapchain::surfaceFormat() const { return swapChainSurfaceFormat; }
vk::Extent2D         Swapchain::extent() const { return swapChainExtent; }
uint32_t             Swapchain::minImageCount() const { return swapChainMinImageCount; }

uint32_t Swapchain::imageCount() const
{
    return static_cast<uint32_t>(swapChainImages.size());
}

void Swapchain::createSwapChain()
{
    vk::SurfaceCapabilitiesKHR surfaceCapabilities = vkCheck(
        vulkan->physicalDeviceHandle().getSurfaceCapabilitiesKHR(vulkan->surfaceHandle()
        ));
    swapChainExtent        = chooseSwapExtent(surfaceCapabilities);
    swapChainMinImageCount = chooseSwapMinImageCount(surfaceCapabilities);

    std::vector<vk::SurfaceFormatKHR> availableFormats = vkCheck(
        vulkan->physicalDeviceHandle().getSurfaceFormatsKHR(vulkan->surfaceHandle()));
    swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    std::vector<vk::PresentModeKHR> availablePresentModes = vkCheck(
        vulkan->physicalDeviceHandle().getSurfacePresentModesKHR(vulkan->surfaceHandle()
        ));
    vk::PresentModeKHR presentMode = chooseSwapPresentMode(availablePresentModes);

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{.surface = vulkan->surfaceHandle(),
                                                   .minImageCount =
                                                   swapChainMinImageCount,
                                                   .imageFormat = swapChainSurfaceFormat.
                                                   format,
                                                   .imageColorSpace =
                                                   swapChainSurfaceFormat.colorSpace,
                                                   .imageExtent = swapChainExtent,
                                                   .imageArrayLayers = 1,
                                                   .imageUsage =
                                                   vk::ImageUsageFlagBits::eColorAttachment,
                                                   .imageSharingMode =
                                                   vk::SharingMode::eExclusive,
                                                   .preTransform = surfaceCapabilities.
                                                   currentTransform,
                                                   .compositeAlpha =
                                                   vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                                   .presentMode = presentMode,
                                                   .clipped = true};

    swapChain = vkCheck(
        vulkan->deviceHandle().createSwapchainKHR(swapChainCreateInfo));
    swapChainImages = vkCheck(swapChain.getImages());
    renderFinishedSemaphores.reserve(swapChainImages.size());
    for (size_t index = 0; index < swapChainImages.size(); ++index)
    {
        renderFinishedSemaphores.push_back(vkCheck(
            vulkan->deviceHandle().createSemaphore(vk::SemaphoreCreateInfo{})));
    }
}

void Swapchain::createImageViews()
{
    assert(swapChainImageViews.empty());

    vk::ImageViewCreateInfo imageViewCreateInfo{.viewType = vk::ImageViewType::e2D,
                                                .format = swapChainSurfaceFormat.format,
                                                .subresourceRange = {
                                                    vk::ImageAspectFlagBits::eColor, 0, 1,
                                                    0, 1}};
    // 一个image view只能绑定到一个image上，而不能复用
    // 一个image对应多个view
    for (auto& image : swapChainImages)
    {
        imageViewCreateInfo.image = image;
        swapChainImageViews.push_back(vkCheck(
            vulkan->deviceHandle().createImageView(imageViewCreateInfo)));
    }
}

uint32_t Swapchain::chooseSwapMinImageCount(
    vk::SurfaceCapabilitiesKHR const& surfaceCapabilities)
{
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount <
        minImageCount))
    {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

vk::SurfaceFormatKHR Swapchain::chooseSwapSurfaceFormat(
    const std::vector<vk::SurfaceFormatKHR>& availableFormats)
{
    assert(!availableFormats.empty());
    const auto formatIt = std::ranges::find_if(
        availableFormats,
        [](const auto& format)
        {
            return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace ==
                vk::ColorSpaceKHR::eSrgbNonlinear;
        });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

vk::PresentModeKHR Swapchain::chooseSwapPresentMode(
    std::vector<vk::PresentModeKHR> const& availablePresentModes)
{
    assert(
        std::ranges::any_of(availablePresentModes, [](auto presentMode) { return
            presentMode == vk::PresentModeKHR::eFifo; }));
    return std::ranges::any_of(availablePresentModes,
                               [](const vk::PresentModeKHR value)
                               {
                                   return vk::PresentModeKHR::eMailbox == value;
                               })
        ? vk::PresentModeKHR::eMailbox
        : vk::PresentModeKHR::eFifo;
}

vk::Extent2D Swapchain::chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    const auto [width, height] = window->framebufferSize();

    return {
        std::clamp<uint32_t>(width,
                             capabilities.minImageExtent.width,
                             capabilities.maxImageExtent.width),
        std::clamp<uint32_t>(height,
                             capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height)};
}
