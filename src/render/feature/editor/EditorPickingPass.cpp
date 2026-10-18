#include "render/feature/editor/EditorPickingPass.h"
#include "core/Context.h"
#include "render/Visibility.h"
#include "render/feature/editor/EditorInterface.h"

#include "core/Logger.h"
#include "render/FrameData.h"
#include "render/PipelineManager.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/feature/RenderGraph.h"
#include "render/feature/editor/EditorFrameInput.h"
#include "render/feature/editor/EditorShaderData.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMesh.h"

#include <algorithm>
#include <array>

EditorPickingPass::Output
EditorPickingPass::setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle depthHandle)
{
    DCHECK(depthHandle.id != RenderGraph::kInvalidHandle);

    pipelines = renderContext().pipelineManager;
    shader    = renderContext().shaderManager->getOrLoad("picking");

    const auto extent = setup.swapchain.extent();
    depth             = depthHandle;
    graph.use(
        depth,
        {
            .stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead,
            .layout = vk::ImageLayout::eDepthReadOnlyOptimal,
        }
    );

    pickingImage = graph.createImage({.format = vk::Format::eR32Uint, .extent = {extent.width, extent.height, 1}});
    pickingImage = graph.write(pickingImage);
    graph.use(
        pickingImage,
        {
            .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .access = vk::AccessFlagBits2::eColorAttachmentWrite,
            .layout = vk::ImageLayout::eColorAttachmentOptimal,
        }
    );

    pipelineLayout = EditorInterface::pipelineLayout(*renderContext().descriptors, *renderContext().pipelineManager);
    auto state     = PipelineState{
            .vertexLayout = VertexLayoutPreset::PositionOnly,
            .depthCompare = vk::CompareOp::eLessOrEqual,
            .depthWrite   = false
    };
    pipeline = pipelines->getOrCreate({
        .shader       = shader,
        .layout       = pipelineLayout,
        .state        = state,
        .colorFormats = {vk::Format::eR32Uint},
        .depthFormat  = vk::Format::eD32Sfloat,
    });
    return {pickingImage};
}

void EditorPickingPass::draw(vk::raii::CommandBuffer& commandBuffer, const DrawItem& item) const
{
    const EditorPickingPushConstants parameters{
        .model       = item.model,
        .selectionId = item.selectionId,
    };
    commandBuffer.pushConstants<EditorPickingPushConstants>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        parameters
    );
    item.mesh->bind(commandBuffer);
    commandBuffer.drawIndexed(item.indexCount, 1, item.firstIndex, 0, 0);
    recordDraw(context().profiler, item.indexCount);
}

void EditorPickingPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution
) const
{
    const auto& editor = execution.editor;
    if (!editor.requestPick)
    {
        return;
    }
    const auto extent = execution.setup.swapchain.extent();
    DCHECK(
        editor.pickX < extent.width && editor.pickY < extent.height,
        "picking coordinate is outside the render target"
    );
    const uint32_t x = editor.pickX;
    const uint32_t y = editor.pickY;

    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = graph.getImage(pickingImage, execution.frameIndex)
                         .getOrCreateImageView({
                             .viewType = vk::ImageViewType::e2D,
                             .format   = vk::Format::eR32Uint,
                             .subresourceRange =
                                 {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
                         }),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = vk::ClearColorValue(std::array<uint32_t, 4>{0u, 0u, 0u, 0u}),
    };
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = graph.getImage(depth, execution.frameIndex)
                         .getOrCreateImageView({
                             .viewType = vk::ImageViewType::e2D,
                             .format   = vk::Format::eD32Sfloat,
                             .subresourceRange =
                                 {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1},
                         }),
        .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        .loadOp      = vk::AttachmentLoadOp::eLoad,
        .storeOp     = vk::AttachmentStoreOp::eNone,
    };
    commandBuffer.beginRendering({
        .renderArea           = {{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {1, 1}},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorAttachment,
        .pDepthAttachment     = &depthAttachment,
    });
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sets = {execution.data.viewSetHandle()};
    commandBuffer
        .bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, EditorInterface::kViewSet, sets, {});

    // Match ScenePass's viewport; only the clicked pixel needs rasterization.
    const auto&    viewport  = execution.view.viewport;
    const uint32_t viewportX = std::min(static_cast<uint32_t>(std::max(viewport.x, 0.0f)), extent.width - 1);
    const uint32_t viewportY = std::min(static_cast<uint32_t>(std::max(viewport.y, 0.0f)), extent.height - 1);
    const uint32_t width     = std::max(1u, std::min(static_cast<uint32_t>(viewport.width), extent.width - viewportX));
    const uint32_t height = std::max(1u, std::min(static_cast<uint32_t>(viewport.height), extent.height - viewportY));
    commandBuffer.setViewport(
        0,
        vk::Viewport(
            static_cast<float>(viewportX),
            static_cast<float>(viewportY),
            static_cast<float>(width),
            static_cast<float>(height),
            0.0f,
            1.0f
        )
    );
    commandBuffer.setScissor(0, vk::Rect2D({static_cast<int32_t>(x), static_cast<int32_t>(y)}, {1, 1}));

    for (const DrawItem& item : execution.renderData.drawItems)
    {
        if (!(item.visibilityMask & RenderVisibility::kMainView))
            continue;

        draw(commandBuffer, item);
    }
    for (const auto& item : editor.lightDrawItems)
    {
        const EditorPickingPushConstants push{
            .model       = item.model,
            .selectionId = item.selectionId,
        };
        commandBuffer.pushConstants<EditorPickingPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            push
        );
        item.mesh->bind(commandBuffer);
        commandBuffer.drawIndexed(item.indexCount, 1, item.firstIndex, 0, 0);
        recordDraw(context().profiler, item.indexCount);
    }
    commandBuffer.endRendering();
}
