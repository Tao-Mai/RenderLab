#include "render/device/GpuUploadContext.h"

#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resources/Buffer.h"

#include <utility>

void GpuUploadContext::copyBuffer(
    const Falcor::Buffer& source, const Falcor::Buffer& destination, const vk::BufferCopy& region
) const
{
    const auto& device = vulkan.deviceHandle();
    const vk::CommandBufferAllocateInfo allocationInfo{
        .commandPool        = commandPool,
        .level              = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    auto commands = std::move(vkCheck(device.allocateCommandBuffers(allocationInfo)).front());
    vkCheck(commands.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));
    const vk::MemoryBarrier2 beforeCopy{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite,
    };
    commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &beforeCopy});
    commands.copyBuffer(source.handle(), destination.handle(), region);

    const bool readBack = destination.getMemoryType() == Falcor::MemoryType::ReadBack;
    const vk::BufferMemoryBarrier2 barrier{
        .srcStageMask        = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask        = readBack ? vk::PipelineStageFlagBits2::eHost : vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask       = readBack ? vk::AccessFlagBits2::eHostRead : vk::AccessFlagBits2::eMemoryRead,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer              = destination.handle(),
        .offset              = region.dstOffset,
        .size                = region.size,
    };
    commands.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier});
    vkCheck(commands.end());

    const auto command = *commands;
    auto& queue = vulkan.queueHandle();
    vkCheck(queue.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command}, nullptr));
    vkCheck(queue.waitIdle());
}
