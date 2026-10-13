#include "render/feature/editor/EditorUiPass.h"

#include "render/feature/editor/EditorFrameInput.h"
#include "render/present/Swapchain.h"

EditorUiPass::Output EditorUiPass::setup(RenderGraph& graph, RenderGraph::ImageHandle colorHandle)
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

void EditorUiPass::execute(RenderGraph& graph) const
{
    const auto& editor = graph.editorInput();
    if (!editor.recordUi)
        return;

    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView   = graph.getImage(color).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = graph.swapchain().surfaceFormat().format,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eLoad,
        .storeOp     = vk::AttachmentStoreOp::eStore,
    };
    graph.commands().beginRendering({
        .renderArea           = {{0, 0}, graph.swapchain().extent()},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorAttachment,
    });
    graph.recordDrawStatistics(editor.recordUi(*graph.commands()));
    graph.commands().endRendering();
}
