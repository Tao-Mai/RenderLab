#include "render/present/Swapchain.h"

#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "core/Window.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <utility>
#include "core/Logger.h"

void Swapchain::init(VulkanContext& context, Window& targetWindow)
{
    vulkan = &context;
    window = &targetWindow;
    createSwapChain();
    createImageViews();
    createDepthResources();
    createPickingResources();
}

void Swapchain::reset() noexcept
{
    pickingImageView = nullptr;
    pickingImage = nullptr;
    pickingImageMemory = nullptr;
    for (DepthTarget& target : depthTargets)
    {
        target.view = nullptr;
        target.image = nullptr;
        target.memory = nullptr;
    }
    depthFormat      = vk::Format::eUndefined;
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

vk::Image Swapchain::depthImageHandle(uint32_t frameIndex) const
{
    return *depthTargets.at(frameIndex).image;
}
vk::ImageView Swapchain::depthImageViewHandle(uint32_t frameIndex) const
{
    return *depthTargets.at(frameIndex).view;
}
vk::Image            Swapchain::pickingImageHandle() const { return *pickingImage; }
vk::ImageView        Swapchain::pickingImageViewHandle() const { return *pickingImageView; }
vk::SurfaceFormatKHR Swapchain::surfaceFormat() const { return swapChainSurfaceFormat; }
vk::Extent2D         Swapchain::extent() const { return swapChainExtent; }
vk::Format           Swapchain::depthImageFormat() const { return depthFormat; }
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
    for (DepthTarget& target : depthTargets)
    {
        target.image = vkCheck(vulkan->deviceHandle().createImage(imageInfo));
        const vk::MemoryRequirements requirements = target.image.getMemoryRequirements();
        const vk::MemoryAllocateInfo allocationInfo{
            .allocationSize = requirements.size,
            .memoryTypeIndex = vulkan_memory::findType(
                vulkan->physicalDeviceHandle(), requirements.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eDeviceLocal),
        };
        target.memory = vkCheck(vulkan->deviceHandle().allocateMemory(allocationInfo));
        vkCheck(target.image.bindMemory(*target.memory, 0));
        const vk::ImageViewCreateInfo viewInfo{
            .image = *target.image,
            .viewType = vk::ImageViewType::e2D,
            .format = depthFormat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        target.view = vkCheck(vulkan->deviceHandle().createImageView(viewInfo));
    }
}

void Swapchain::createPickingResources()
{
    constexpr vk::Format format = vk::Format::eR32Uint;
    const vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {swapChainExtent.width, swapChainExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment |
            vk::ImageUsageFlagBits::eTransferSrc,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    pickingImage = vkCheck(vulkan->deviceHandle().createImage(imageInfo));
    const vk::MemoryRequirements requirements = pickingImage.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocationInfo{
        .allocationSize = requirements.size,
        .memoryTypeIndex = vulkan_memory::findType(
            vulkan->physicalDeviceHandle(), requirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal),
    };
    pickingImageMemory = vkCheck(vulkan->deviceHandle().allocateMemory(allocationInfo));
    vkCheck(pickingImage.bindMemory(*pickingImageMemory, 0));
    const vk::ImageViewCreateInfo viewInfo{
        .image = *pickingImage,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    pickingImageView = vkCheck(vulkan->deviceHandle().createImageView(viewInfo));
}

void Swapchain::transitionDepthImageLayout(const vk::raii::CommandPool& commandPool)
{
    const vk::CommandBufferAllocateInfo allocationInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    vk::raii::CommandBuffer transitionCommand = std::move(vkCheck(
        vulkan->deviceHandle().allocateCommandBuffers(allocationInfo)).front());
    vkCheck(
        transitionCommand.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}
        ));

    for (const DepthTarget& target : depthTargets)
    {
        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests,
            .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *target.image,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        const vk::DependencyInfo dependencyInfo{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        transitionCommand.pipelineBarrier2(dependencyInfo);
    }
    vkCheck(transitionCommand.end());

    const vk::CommandBuffer command = *transitionCommand;
    const vk::SubmitInfo    submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    vkCheck(vulkan->queueHandle().submit(submitInfo, nullptr));
    vkCheck(vulkan->queueHandle().waitIdle());
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
    LOG_FATAL("failed to find a supported depth format");
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
