#include "render/device/Swapchain.h"

#include "core/Window.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include "core/Logger.h"

void Swapchain::init(VulkanContext& context, Window& targetWindow)
{
    vulkan = &context;
    window = &targetWindow;
    createSwapChain();
}

void Swapchain::reset() noexcept
{
    renderFinishedSemaphores.clear();
    framebuffers.clear();
    swapChain              = nullptr;
    swapChainMinImageCount = 0;
    window                 = nullptr;
    vulkan                 = nullptr;
}

vk::ResultValue<uint32_t> Swapchain::acquire(vk::Semaphore imageAvailable) const
{
    uint32_t   imageIndex = 0;
    const auto result     = static_cast<vk::Result>(swapChain.getDispatcher()->vkAcquireNextImageKHR(
        static_cast<VkDevice>(*vulkan->deviceHandle()),
        static_cast<VkSwapchainKHR>(*swapChain),
        UINT64_MAX,
        static_cast<VkSemaphore>(imageAvailable),
        VK_NULL_HANDLE,
        &imageIndex
    ));
    CHECK(
        result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR ||
            result == vk::Result::eErrorOutOfDateKHR,
        "vkAcquireNextImageKHR failed: {}",
        vk::to_string(result)
    );
    return {result, imageIndex};
}

vk::Result Swapchain::present(uint32_t imageIndex) const
{
    const auto               renderFinished = renderFinishedSemaphore(imageIndex);
    const vk::PresentInfoKHR info{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &renderFinished,
        .swapchainCount     = 1,
        .pSwapchains        = &*swapChain,
        .pImageIndices      = &imageIndex,
    };
    const auto result = static_cast<vk::Result>(vulkan->queueHandle().getDispatcher()->vkQueuePresentKHR(
        static_cast<VkQueue>(*vulkan->queueHandle()),
        reinterpret_cast<const VkPresentInfoKHR*>(&info)
    ));
    CHECK(
        result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR ||
            result == vk::Result::eErrorOutOfDateKHR,
        "vkQueuePresentKHR failed: {}",
        vk::to_string(result)
    );
    return result;
}

vk::Semaphore Swapchain::renderFinishedSemaphore(uint32_t index) const { return *renderFinishedSemaphores.at(index); }

void Swapchain::createSwapChain()
{
    vk::SurfaceCapabilitiesKHR surfaceCapabilities =
        vkCheck(vulkan->physicalDeviceHandle().getSurfaceCapabilitiesKHR(vulkan->surfaceHandle()));
    swapChainExtent        = chooseSwapExtent(surfaceCapabilities);
    swapChainMinImageCount = chooseSwapMinImageCount(surfaceCapabilities);

    std::vector<vk::SurfaceFormatKHR> availableFormats =
        vkCheck(vulkan->physicalDeviceHandle().getSurfaceFormatsKHR(vulkan->surfaceHandle()));
    swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    std::vector<vk::PresentModeKHR> availablePresentModes =
        vkCheck(vulkan->physicalDeviceHandle().getSurfacePresentModesKHR(vulkan->surfaceHandle()));
    const auto presentMode = chooseSwapPresentMode(availablePresentModes);
    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface          = vulkan->surfaceHandle(),
        .minImageCount    = swapChainMinImageCount,
        .imageFormat      = swapChainSurfaceFormat.format,
        .imageColorSpace  = swapChainSurfaceFormat.colorSpace,
        .imageExtent      = swapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform     = surfaceCapabilities.currentTransform,
        .compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode      = presentMode,
        .clipped          = true
    };

    swapChain         = vkCheck(vulkan->deviceHandle().createSwapchainKHR(swapChainCreateInfo));
    const auto images = vkCheck(swapChain.getImages());
    framebuffers.reserve(images.size());
    const vk::ImageCreateInfo info{
        .imageType   = vk::ImageType::e2D,
        .format      = swapChainSurfaceFormat.format,
        .extent      = {swapChainExtent.width, swapChainExtent.height, 1},
        .mipLevels   = 1,
        .arrayLayers = swapChainCreateInfo.imageArrayLayers,
        .samples     = vk::SampleCountFlagBits::e1,
        .usage       = swapChainCreateInfo.imageUsage,
    };
    for (const auto image : images)
        framebuffers.emplace_back().attachColorTarget(
            Falcor::make_ref<Falcor::Texture>(*vulkan, image, info, Falcor::ResourceBindFlags::RenderTarget), 0
        );
    renderFinishedSemaphores.reserve(framebuffers.size());
    for (size_t index = 0; index < framebuffers.size(); ++index)
    {
        renderFinishedSemaphores.push_back(vkCheck(vulkan->deviceHandle().createSemaphore(vk::SemaphoreCreateInfo{})));
    }
}

uint32_t Swapchain::chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities)
{
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
    {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

vk::SurfaceFormatKHR Swapchain::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
{
    assert(!availableFormats.empty());
    const auto formatIt = std::ranges::find_if(
        availableFormats,
        [](const auto& format)
        { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; }
    );
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

vk::PresentModeKHR Swapchain::chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes)
{
    assert(
        std::ranges::any_of(
            availablePresentModes,
            [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }
        )
    );
    return std::ranges::any_of(
               availablePresentModes,
               [](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }
           )
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
        std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
    };
}
