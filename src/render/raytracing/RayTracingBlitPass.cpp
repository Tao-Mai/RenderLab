#include "render/raytracing/RayTracingBlitPass.h"

#include "core/Logger.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/feature/RenderGraph.h"
#include "render/present/Swapchain.h"

#include <array>

RayTracingBlitPass::Output RayTracingBlitPass::setup(
    RenderGraph& graph, RenderGraph::ImageHandle rayImageHandle, RenderGraph::ImageHandle colorHandle
)
{
    const auto& device       = graph.vulkan().physicalDeviceHandle();
    const auto  capabilities = vkCheck(device.getSurfaceCapabilitiesKHR(graph.vulkan().surfaceHandle()));
    CHECK(
        capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst,
        "ray tracing requires transfer-destination swapchain images"
    );
    CHECK(
        device.getFormatProperties(graph.swapchain().surfaceFormat().format).optimalTilingFeatures &
            vk::FormatFeatureFlagBits::eBlitDst,
        "swapchain format does not support blit destinations"
    );
    CHECK(
        device.getFormatProperties(graph.imageInfo(rayImageHandle).format).optimalTilingFeatures &
            vk::FormatFeatureFlagBits::eBlitSrc,
        "ray tracing output format does not support blit sources"
    );

    rayImage = rayImageHandle;
    color    = graph.write(colorHandle);
    graph.use(
        rayImage,
        {
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferRead,
            .layout = vk::ImageLayout::eTransferSrcOptimal,
        }
    );
    graph.use(
        color,
        {
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
            .layout = vk::ImageLayout::eTransferDstOptimal,
        }
    );
    return {color};
}

void RayTracingBlitPass::execute(RenderGraph& graph) const
{
    const auto          extent = graph.swapchain().extent();
    const vk::ImageBlit region{
        .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .srcOffsets =
            std::array{
                vk::Offset3D{0, 0, 0},
                vk::Offset3D{static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1}
            },
        .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .dstOffsets     = std::array{
            vk::Offset3D{0, 0, 0},
            vk::Offset3D{static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1}
        },
    };
    graph.commands().blitImage(
        graph.getImage(rayImage).handle(),
        vk::ImageLayout::eTransferSrcOptimal,
        graph.getImage(color).handle(),
        vk::ImageLayout::eTransferDstOptimal,
        region,
        vk::Filter::eNearest
    );
}
