#include "render/feature/editor/EditorPickingReadbackPass.h"

#include "render/resource/Buffer.h"

EditorPickingReadbackPass::Output
EditorPickingReadbackPass::setup(RenderGraph& graph, RenderGraph::BufferHandle readbackHandle)
{
    readback = readbackHandle;
    graph.use(readback, {.stages = vk::PipelineStageFlagBits2::eHost, .access = vk::AccessFlagBits2::eHostRead});

    return {};
}

void EditorPickingReadbackPass::execute(RenderGraph&) const
{
    // Graph applies HostRead here; the CPU reads only after the submit fence completes.
}

uint32_t EditorPickingReadbackPass::readSelectionId(RenderGraph& graph, uint32_t frameIndex) const
{
    uint32_t selectionId = 0;
    graph.getBuffer(readback, frameIndex).download(&selectionId, sizeof(selectionId));
    return selectionId;
}
