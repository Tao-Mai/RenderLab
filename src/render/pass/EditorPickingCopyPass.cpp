#include "render/pass/EditorPickingCopyPass.h"

#include "core/Logger.h"
#include "render/Renderer.h"
#include "render/pass/EditorPickingPass.h"
#include "render/pass/RenderGraph.h"
#include "render/resource/Buffer.h"

void EditorPickingCopyPass::registerPass(RenderGraph& graph)
{
    std::tie(inputSlots, outputSlots) = graph.registerPass(
        "editor_picking_copy",
        InputCount,
        OutputCount,
        [this](RenderGraph& g) { setupPass(g); },
        [this](RenderGraph& g) { executePass(g); }
    );
}

void EditorPickingCopyPass::setupPass(RenderGraph& graph)
{
    graph.bindInput(
        inputSlots[PickingImage],
        graph.outputSlot("editor_picking", EditorPickingPass::ImageResult),
        {
            .imageUsage = vk::ImageUsageFlagBits::eTransferSrc,
            .stages     = vk::PipelineStageFlagBits2::eTransfer,
            .access     = vk::AccessFlagBits2::eTransferRead,
            .layout     = vk::ImageLayout::eTransferSrcOptimal,
        }
    );
    graph.createResource(
        inputSlots[Readback],
        {
            .type             = RenderGraph::ResourceType::Buffer,
            .size             = sizeof(uint32_t),
            .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            .bufferUsage      = vk::BufferUsageFlagBits::eTransferDst,
            .stages           = vk::PipelineStageFlagBits2::eTransfer,
            .access           = vk::AccessFlagBits2::eTransferWrite,
        }
    );
    graph.bindOutput(outputSlots[ReadbackResult], inputSlots[Readback]);
}

void EditorPickingCopyPass::executePass(RenderGraph& graph) const
{
    const auto& editor = graph.editorInput();
    if (!editor.requestPick)
        return;

    const auto& extent = graph.resourceDesc(inputSlots[PickingImage]).extent;
    DCHECK(editor.pickX < extent.width && editor.pickY < extent.height);
    const vk::BufferImageCopy copyRegion{
        .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
        .imageOffset      = {static_cast<int32_t>(editor.pickX), static_cast<int32_t>(editor.pickY), 0},
        .imageExtent      = {1, 1, 1},
    };
    graph.commands().copyImageToBuffer(
        graph.image(inputSlots[PickingImage]),
        vk::ImageLayout::eTransferSrcOptimal,
        graph.buffer(inputSlots[Readback]).handle(),
        copyRegion
    );
}
