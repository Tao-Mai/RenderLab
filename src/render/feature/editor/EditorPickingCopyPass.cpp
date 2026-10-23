#include "render/feature/editor/EditorPickingCopyPass.h"

#include "core/Logger.h"
#include "render/feature/editor/EditorFrameInput.h"
#include "render/resource/Buffer.h"

EditorPickingCopyPass::Output
EditorPickingCopyPass::setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle pickingImageHandle)
{
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
        .scope            = RenderGraph::ResourceScope::PerFrame,
    });
    readback = graph.write(readback);
    graph.use(
        readback,
        {
            .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite,
        }
    );
    return {readback};
}

void EditorPickingCopyPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution
) const
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
        graph.getBuffer(readback, execution.frameIndex).handle(),
        copyRegion
    );
}
