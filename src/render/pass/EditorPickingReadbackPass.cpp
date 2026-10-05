#include "render/pass/EditorPickingReadbackPass.h"

#include "render/pass/EditorPickingCopyPass.h"
#include "render/pass/RenderGraph.h"
#include "render/resource/Buffer.h"

void EditorPickingReadbackPass::registerPass(RenderGraph& graph)
{
    std::tie(inputSlots, std::ignore) = graph.registerPass(
        "editor_picking_readback",
        InputCount,
        0,
        [this](RenderGraph& g) { setupPass(g); },
        [](RenderGraph&)
        {
            // Graph applies HostRead before this node; the CPU reads after the submit fence.
        }
    );
}

void EditorPickingReadbackPass::setupPass(RenderGraph& graph)
{
    graph.bindInput(
        inputSlots[Readback],
        graph.outputSlot("editor_picking_copy", EditorPickingCopyPass::ReadbackResult),
        {
            .type   = RenderGraph::ResourceType::Buffer,
            .stages = vk::PipelineStageFlagBits2::eHost,
            .access = vk::AccessFlagBits2::eHostRead,
        }
    );
}

uint32_t EditorPickingReadbackPass::readSelectionId(RenderGraph& graph, uint32_t frameIndex) const
{
    uint32_t selectionId = 0;
    graph.buffer(inputSlots[Readback], frameIndex).download(&selectionId, sizeof(selectionId));
    return selectionId;
}
