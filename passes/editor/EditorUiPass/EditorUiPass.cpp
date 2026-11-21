#include "passes/editor/EditorUiPass/EditorUiPass.h"
#include "render/plugins/PluginManager.h"

#include "render/editor/EditorFrameInput.h"

RenderPassReflection EditorUiPass::reflect(const PassSetup&) const
{
    RenderPassReflection reflection;
    reflection.input("color", PortKind::Image);
    reflection.output("color", PortKind::Image);
    return reflection;
}

void EditorUiPass::compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports)
{
    const auto colorHandle = ports.image("color");

    color = graph.write(colorHandle);
    graph.use(
        color,
        {
            .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .access = vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
            .layout = vk::ImageLayout::eColorAttachmentOptimal,
        }
    );
    ports.setOutput("color", color);
}

void EditorUiPass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution)
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

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<EditorUiPass>("EditorUiPass");
}
