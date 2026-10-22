#include "render/feature/editor/EditorDepthPass.h"
#include "core/Context.h"
#include "render/Visibility.h"

#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/feature/editor/EditorShaderData.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMesh.h"

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

    auto& device   = *renderContext().device;
    pipelineLayout = renderContext().device->editorPipelineLayout;
    pipeline       = device.getOrCreatePipeline({
              .shader      = renderContext().shaderManager->getOrLoad("editor_depth"),
              .layout      = pipelineLayout,
              .state       = PipelineState{.vertexLayout = VertexLayoutPreset::PositionOnly},
              .depthFormat = vk::Format::eD32Sfloat,
    });
    return {depth};
}

void EditorDepthPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution
) const
{
    const auto                        extent = execution.setup.swapchain.extent();
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView   = graph.getImage(depth).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = vk::Format::eD32Sfloat,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1},
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

    const auto viewport = execution.view.viewport.clamped(extent);
    commandBuffer.setViewport(0, viewport.toViewport());
    commandBuffer.setScissor(0, viewport.toScissor());

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array viewSets = {execution.data.viewSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        EditorInterface::kViewSet,
        viewSets,
        {}
    );
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
        recordDraw(context().profiler, item.indexCount);
    }
    commandBuffer.endRendering();
}
