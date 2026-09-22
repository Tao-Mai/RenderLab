#include "render/present/swapchain.h"

#include "render/device/memory.h"
#include "render/device/vulkan_context.h"
#include "window.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <utility>

void Swapchain::initialize(VulkanContext& context, Window& targetWindow)
{
    vulkan = &context;
    window = &targetWindow;
    try
    {
        createSwapChain();
        createImageViews();
        createDepthResources();
    }
    catch (...)
    {
        reset();
        throw;
    }
}

void Swapchain::reset() noexcept
{
    depthImageView = nullptr;
    depthImage = nullptr;
    depthImageMemory = nullptr;
    depthFormat = vk::Format::eUndefined;
    swapChainImageViews.clear();
    swapChainImages.clear();
    swapChain = nullptr;
    swapChainMinImageCount = 0;
    window = nullptr;
    vulkan = nullptr;
}

const vk::raii::SwapchainKHR& Swapchain::handle() const { return swapChain; }
vk::Image Swapchain::image(uint32_t index) const { return swapChainImages[index]; }
vk::ImageView Swapchain::imageView(uint32_t index) const { return *swapChainImageViews[index]; }
vk::Image Swapchain::depthImageHandle() const { return *depthImage; }
vk::ImageView Swapchain::depthImageViewHandle() const { return *depthImageView; }
vk::SurfaceFormatKHR Swapchain::surfaceFormat() const { return swapChainSurfaceFormat; }
vk::Extent2D Swapchain::extent() const { return swapChainExtent; }
vk::Format Swapchain::depthImageFormat() const { return depthFormat; }
uint32_t Swapchain::minImageCount() const { return swapChainMinImageCount; }
uint32_t Swapchain::imageCount() const { return static_cast<uint32_t>(swapChainImages.size()); }

void Swapchain::createSwapChain()
{
    vk::SurfaceCapabilitiesKHR surfaceCapabilities = vulkan->physicalDeviceHandle().
        getSurfaceCapabilitiesKHR(vulkan->surfaceHandle());
    swapChainExtent        = chooseSwapExtent(surfaceCapabilities);
    swapChainMinImageCount = chooseSwapMinImageCount(surfaceCapabilities);

    std::vector<vk::SurfaceFormatKHR> availableFormats = vulkan->physicalDeviceHandle().
        getSurfaceFormatsKHR(vulkan->surfaceHandle());
    swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    std::vector<vk::PresentModeKHR> availablePresentModes = vulkan->physicalDeviceHandle().
        getSurfacePresentModesKHR(vulkan->surfaceHandle());
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

    swapChain       = vk::raii::SwapchainKHR(vulkan->deviceHandle(), swapChainCreateInfo);
    swapChainImages = swapChain.getImages();
}

void Swapchain::createImageViews()
{
    assert(swapChainImageViews.empty());

    vk::ImageViewCreateInfo imageViewCreateInfo{.viewType = vk::ImageViewType::e2D,
                                                .format = swapChainSurfaceFormat.format,
                                                .subresourceRange = {
                                                    vk::ImageAspectFlagBits::eColor, 0, 1,
                                                    0, 1}};
    for (auto& image : swapChainImages)
    {
        imageViewCreateInfo.image = image;
        swapChainImageViews.emplace_back(vulkan->deviceHandle(), imageViewCreateInfo);
    }
}

void Swapchain::createDepthResources()
{
    depthFormat = chooseDepthFormat();
    const vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = depthFormat,
        .extent = {swapChainExtent.width, swapChainExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    depthImage = vk::raii::Image(vulkan->deviceHandle(), imageInfo);

    const vk::MemoryRequirements requirements = depthImage.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocationInfo{
        .allocationSize = requirements.size,
        .memoryTypeIndex = vulkan_memory::findType(
            vulkan->physicalDeviceHandle(),
            requirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal),
    };
    depthImageMemory = vk::raii::DeviceMemory(vulkan->deviceHandle(), allocationInfo);
    depthImage.bindMemory(*depthImageMemory, 0);

    const vk::ImageViewCreateInfo viewInfo{
        .image = *depthImage,
        .viewType = vk::ImageViewType::e2D,
        .format = depthFormat,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eDepth,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    depthImageView = vk::raii::ImageView(vulkan->deviceHandle(), viewInfo);
}

void Swapchain::transitionDepthImageLayout(const vk::raii::CommandPool& commandPool)
{
    const vk::CommandBufferAllocateInfo allocationInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    vk::raii::CommandBuffer transitionCommand =
        std::move(vk::raii::CommandBuffers(vulkan->deviceHandle(), allocationInfo).front());
    transitionCommand.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccessMask = {},
        .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests,
        .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *depthImage,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eDepth,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    const vk::DependencyInfo dependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    transitionCommand.pipelineBarrier2(dependencyInfo);
    transitionCommand.end();

    const vk::CommandBuffer command = *transitionCommand;
    const vk::SubmitInfo    submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    vulkan->queueHandle().submit(submitInfo, nullptr);
    vulkan->queueHandle().waitIdle();
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

vk::Format Swapchain::chooseDepthFormat() const
{
    constexpr std::array candidates = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
    };
    for (const vk::Format format : candidates)
    {
        const vk::FormatProperties properties = vulkan->physicalDeviceHandle().
            getFormatProperties(format);
        if ((properties.optimalTilingFeatures &
                vk::FormatFeatureFlagBits::eDepthStencilAttachment) !=
            vk::FormatFeatureFlags
            {})
        {
            return format;
        }
    }
    throw std::runtime_error("failed to find a supported depth format");
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

