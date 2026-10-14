#include "render/feature/editor/EditorDepthPass.h"
#include "render/Visibility.h"
#include "render/feature/editor/EditorInterface.h"

#include "render/FrameData.h"
#include "render/PipelineManager.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/device/VulkanContext.h"
#include "render/feature/editor/EditorShaderData.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMesh.h"

#include <algorithm>
#include <array>

EditorDepthPass::Output EditorDepthPass::setup(RenderGraph& graph, const PassSetup& setup)
{
    CHECK(
        setup.vulkan.physicalDeviceHandle().getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );
    const auto extent = setup.swapchain.extent();
    depth = graph.createImage({.format = vk::Format::eD32Sfloat, .extent = {extent.width, extent.height, 1}});
    depth = graph.write(depth);
    graph.use(
        depth,
        {
            .stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            .access =
                vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            .layout = vk::ImageLayout::eDepthAttachmentOptimal,
        }
    );

    auto& pipelines = *renderContext().pipelineManager;
    pipelineLayout  = EditorInterface::pipelineLayout(*renderContext().descriptors, *renderContext().pipelineManager);
    pipeline        = pipelines.getOrCreate({
               .shader      = renderContext().shaderManager->getOrLoad("editor_depth"),
               .layout      = pipelineLayout,
               .state       = PipelineState{.vertexLayout = VertexLayoutPreset::PositionOnly},
               .depthFormat = vk::Format::eD32Sfloat,
    });
    return {depth};
}

void EditorDepthPass::execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution) const
{
    const auto                        extent = execution.setup.swapchain.extent();
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = graph.getImage(depth, execution.frameIndex)
                         .getOrCreateImageView({
                             .viewType = vk::ImageViewType::e2D,
                             .format   = vk::Format::eD32Sfloat,
                             .subresourceRange =
                                 {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1},
                         }),
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = vk::ClearDepthStencilValue(1.0f, 0),
    };
    commandBuffer.beginRendering({
        .renderArea       = {.offset = {0, 0}, .extent = extent},
        .layerCount       = 1,
        .pDepthAttachment = &depthAttachment,
    });

    const auto&    editorViewport = execution.view.viewport;
    const uint32_t viewportX      = std::min(static_cast<uint32_t>(std::max(editorViewport.x, 0.0f)), extent.width - 1);
    const uint32_t viewportY = std::min(static_cast<uint32_t>(std::max(editorViewport.y, 0.0f)), extent.height - 1);
    const uint32_t viewportWidth =
        std::max(1u, std::min(static_cast<uint32_t>(editorViewport.width), extent.width - viewportX));
    const uint32_t viewportHeight =
        std::max(1u, std::min(static_cast<uint32_t>(editorViewport.height), extent.height - viewportY));
    commandBuffer.setViewport(
        0,
        vk::Viewport(
            static_cast<float>(viewportX),
            static_cast<float>(viewportY),
            static_cast<float>(viewportWidth),
            static_cast<float>(viewportHeight),
            0.0f,
            1.0f
        )
    );
    commandBuffer.setScissor(
        0,
        vk::Rect2D({static_cast<int32_t>(viewportX), static_cast<int32_t>(viewportY)}, {viewportWidth, viewportHeight})
    );

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array viewSets = {execution.data.viewSetHandle()};
    commandBuffer
        .bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, EditorInterface::kViewSet, viewSets, {});
    for (const DrawItem& item : execution.renderData.drawItems)
    {
        if (!(item.visibilityMask & RenderVisibility::kMainView))
            continue;

        const EditorDepthPushConstants parameters{.model = item.model};
        commandBuffer.pushConstants<EditorDepthPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            parameters
        );
        item.mesh->bind(commandBuffer);
        commandBuffer.drawIndexed(item.indexCount, 1, item.firstIndex, 0, 0);
    }
    commandBuffer.endRendering();
}
