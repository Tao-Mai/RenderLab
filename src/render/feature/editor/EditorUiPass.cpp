#include "render/feature/editor/EditorUiPass.h"

#include "render/feature/editor/EditorFrameInput.h"

EditorUiPass::Output
EditorUiPass::setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle colorHandle)
{
    color = graph.write(colorHandle);
    graph.use(
        color,
        {
            .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .access = vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            .layout = vk::ImageLayout::eColorAttachmentOptimal,
        }
    );
    return {color};
}

void EditorUiPass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
{
    const auto& editor = execution.editor;
    if (!editor.recordUi)
        return;

    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView   = graph.getImage(color).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = graph.imageInfo(color).format,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eLoad,
        .storeOp     = vk::AttachmentStoreOp::eStore,
    };
    commandBuffer.beginRendering({
        .renderArea           = {{0, 0}, graph.imageExtent(color)},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorAttachment,
    });
    editor.recordUi(*commandBuffer);
    commandBuffer.endRendering();
}
