#pragma once

#include "render/ShaderManager.h"
#include "render/feature/RenderGraph.h"

#include <span>

struct DrawItem;

namespace SceneDraw
{
void useAttachments(
    RenderGraph& graph, std::span<const RenderGraph::ImageHandle> colors, RenderGraph::ImageHandle depth
);
vk::Rect2D beginRendering(
    RenderGraph&                              graph,
    std::span<const RenderGraph::ImageHandle> colors,
    RenderGraph::ImageHandle                  depth,
    vk::AttachmentLoadOp                      colorLoadOp
);
void recordMeshes(
    RenderGraph&                graph,
    std::span<const DrawItem>   items,
    std::span<const vk::Format> colorFormats,
    ShaderHandle                shader = {}
);
}  // namespace SceneDraw
