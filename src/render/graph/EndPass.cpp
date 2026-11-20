#include "render/graph/EndPass.h"

void EndPass::compile(
    RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle color, RenderGraph::BufferHandle readback
)
{
    if (color.id != RenderGraph::kInvalidHandle)
        graph.use(color, {.layout = vk::ImageLayout::ePresentSrcKHR});

    if (readback.id != RenderGraph::kInvalidHandle)
        graph.use(readback, {.stages = vk::PipelineStageFlagBits2::eHost, .access = vk::AccessFlagBits2::eHostRead});

    graph.retainStage();
}
