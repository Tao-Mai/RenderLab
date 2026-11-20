#include "passes/editor/EditorPickingCopyPass/EditorPickingCopyPass.h"
#include "render/plugins/PluginManager.h"

#include "core/Logger.h"
#include "render/editor/EditorFrameInput.h"
#include "render/resources/Buffer.h"

RenderPassReflection EditorPickingCopyPass::reflect(const PassSetup&) const
{
    RenderPassReflection reflection;
    reflection.input("pickingImage", PortKind::Image, false, vk::Format::eR32Uint);
    reflection.output("readback", PortKind::Buffer);
    return reflection;
}

void EditorPickingCopyPass::compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports)
{
    const auto pickingImageHandle = ports.image("pickingImage");

    pickingImage = pickingImageHandle;
    graph.use(
        pickingImage,
        {
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferRead,
            .layout = vk::ImageLayout::eTransferSrcOptimal,
        }
    );
    readback = graph.createBuffer({
        .size             = sizeof(uint32_t),
        .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
    });
    readback = graph.write(readback);
    graph.use(
        readback,
        {
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        }
    );
    ports.setOutput("readback", readback);
}

void EditorPickingCopyPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution
)
{
    const auto& editor = execution.editor;
    if (!editor.requestPick)
        return;

    const auto extent = graph.imageExtent(pickingImage);
    DCHECK(editor.pickX < extent.width && editor.pickY < extent.height);
    const vk::BufferImageCopy copyRegion{
        .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
        .imageOffset      = {static_cast<int32_t>(editor.pickX), static_cast<int32_t>(editor.pickY), 0},
        .imageExtent      = {1, 1, 1},
    };
    commandBuffer.copyImageToBuffer(
        graph.getImage(pickingImage).handle(),
        vk::ImageLayout::eTransferSrcOptimal,
        graph.getBuffer(readback).handle(),
        copyRegion
    );
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<EditorPickingCopyPass>();
}
