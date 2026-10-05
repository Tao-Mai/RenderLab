#include "render/raytracing/RayTracingBlitPass.h"

#include "core/Logger.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/pass/RenderGraph.h"
#include "render/present/Swapchain.h"
#include "render/raytracing/RayTracingPass.h"

#include <array>

void RayTracingBlitPass::registerPass(RenderGraph& graph)
{
    std::tie(inputSlots, outputSlots) = graph.registerPass(
        "raytracing_blit",
        InputCount,
        OutputCount,
        [this](RenderGraph& g) { setupPass(g); },
        [this](RenderGraph& g) { executePass(g); }
    );
}

void RayTracingBlitPass::setupPass(RenderGraph& graph)
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
        device.getFormatProperties(vk::Format::eR8G8B8A8Unorm).optimalTilingFeatures &
            vk::FormatFeatureFlagBits::eBlitSrc,
        "ray tracing output format does not support blit sources"
    );

    graph.bindInput(
        inputSlots[RayImage],
        graph.outputSlot("raytracing", RayTracingPass::RayImageResult),
        {
            .imageUsage = vk::ImageUsageFlagBits::eTransferSrc,
            .stages     = vk::PipelineStageFlagBits2::eTransfer,
            .access     = vk::AccessFlagBits2::eTransferRead,
            .layout     = vk::ImageLayout::eTransferSrcOptimal,
        }
    );
    graph.importSwapchain(
        inputSlots[Color],
        {
            .imageUsage = vk::ImageUsageFlagBits::eTransferDst,
            .stages     = vk::PipelineStageFlagBits2::eTransfer,
            .access     = vk::AccessFlagBits2::eTransferWrite,
            .layout     = vk::ImageLayout::eTransferDstOptimal,
        }
    );
    graph.bindOutput(outputSlots[ColorResult], inputSlots[Color]);
}

void RayTracingBlitPass::executePass(RenderGraph& graph) const
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
        graph.image(inputSlots[RayImage]),
        vk::ImageLayout::eTransferSrcOptimal,
        graph.image(inputSlots[Color]),
        vk::ImageLayout::eTransferDstOptimal,
        region,
        vk::Filter::eNearest
    );
}
