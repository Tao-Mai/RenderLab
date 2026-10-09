#pragma once

#include "render/feature/RenderGraph.h"

#include <span>

struct DrawItem;

namespace SceneDraw
{
void       useAttachments(RenderGraph& graph, RenderGraph::ImageHandle color, RenderGraph::ImageHandle depth);
vk::Rect2D beginRendering(
    RenderGraph& graph, RenderGraph::ImageHandle color, RenderGraph::ImageHandle depth, vk::AttachmentLoadOp colorLoadOp
);
void recordMeshes(RenderGraph& graph, std::span<const DrawItem> items, vk::Format colorFormat);
}  // namespace SceneDraw
