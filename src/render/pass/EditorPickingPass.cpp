#include "render/pass/EditorPickingPass.h"

#include "render/PipelineManager.h"
#include "render/present/Swapchain.h"
#include "render/resource/Mesh.h"
#include "render/resource/ShaderData.h"

#include <array>

void EditorPickingPass::init(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device& device,
    Swapchain& targetSwapchain,
    PipelineManager& targetPipelines,
    ShaderHandle targetShader)
{
    swapchain = &targetSwapchain;
    pipelines = &targetPipelines;
    shader = targetShader;
    readbackBuffer = Buffer(
        physicalDevice, device, sizeof(uint32_t),
        vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent);
    refreshPipeline();
}

void EditorPickingPass::refreshPipeline()
{
    const PipelineKey key{
        .shader = shader,
        .layout = PipelineLayoutPreset::SceneOnly,
        .state = PipelineState::preset(RenderMode::Picking),
        .colorFormat = vk::Format::eR32Uint,
        .depthFormat = swapchain->depthImageFormat(),
    };
    pipeline = pipelines->getOrCreate(key);
    pipelineLayout = pipelines->layout(PipelineLayoutPreset::SceneOnly);
}

void EditorPickingPass::reset() noexcept
{
    readbackBuffer.reset();
    pipeline = nullptr;
    pipelineLayout = nullptr;
    shader = {};
    pipelines = nullptr;
    swapchain = nullptr;
}

void EditorPickingPass::begin(
    vk::raii::CommandBuffer& commandBuffer,
    vk::Image depthImage,
    vk::ImageView depthImageView,
    vk::DescriptorSet sceneDescriptorSet,
    uint32_t x,
    uint32_t y) const
{
    const std::array barriers = {
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchain->pickingImageHandle(),
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        },
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests,
            .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests,
            .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead,
            .oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = depthImage,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        },
    };
    const vk::DependencyInfo dependency{
        .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
        .pImageMemoryBarriers = barriers.data(),
    };
    commandBuffer.pipelineBarrier2(dependency);

    const vk::ClearValue clear = vk::ClearColorValue(
        std::array<uint32_t, 4>{0u, 0u, 0u, 0u});
    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = swapchain->pickingImageViewHandle(),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clear,
    };
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
    };
    const vk::RenderingInfo renderingInfo{
        .renderArea = {
            .offset = {static_cast<int32_t>(x), static_cast<int32_t>(y)},
            .extent = {1, 1},
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };
    commandBuffer.beginRendering(renderingInfo);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sets = {sceneDescriptorSet};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});
    commandBuffer.setScissor(
        0,
        vk::Rect2D(
            vk::Offset2D(static_cast<int32_t>(x), static_cast<int32_t>(y)),
            vk::Extent2D(1, 1)));
}

void EditorPickingPass::draw(
    vk::raii::CommandBuffer& commandBuffer,
    Mesh& mesh,
    const glm::mat4& model,
    uint32_t selectionId) const
{
    const EditorPickingPushConstants pushConstants{
        .model = model,
        .selectionId = selectionId,
    };
    commandBuffer.pushConstants<EditorPickingPushConstants>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        pushConstants);
    mesh.bind(commandBuffer);
    for (const Submesh& submesh : mesh.submeshes())
    {
        commandBuffer.drawIndexed(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
    }
}

void EditorPickingPass::end(
    vk::raii::CommandBuffer& commandBuffer,
    uint32_t x,
    uint32_t y) const
{
    commandBuffer.endRendering();
    const vk::ImageMemoryBarrier2 readBarrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchain->pickingImageHandle(),
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    const vk::DependencyInfo dependency{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &readBarrier,
    };
    commandBuffer.pipelineBarrier2(dependency);

    const vk::BufferImageCopy copyRegion{
        .imageSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .layerCount = 1,
        },
        .imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0},
        .imageExtent = {1, 1, 1},
    };
    commandBuffer.copyImageToBuffer(
        swapchain->pickingImageHandle(),
        vk::ImageLayout::eTransferSrcOptimal,
        readbackBuffer.handle(),
        copyRegion);
}

uint32_t EditorPickingPass::readSelectionId()
{
    uint32_t selectionId = 0;
    readbackBuffer.download(&selectionId, sizeof(selectionId));
    return selectionId;
}

vk::PipelineLayout EditorPickingPass::layout() const
{
    return pipelineLayout;
}
