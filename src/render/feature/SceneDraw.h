#pragma once

#include "render/ShaderManager.h"
#include "render/feature/RenderGraph.h"

#include <span>

struct DrawItem;

struct SceneShaderOptions
{
    bool ibl       = true;
    bool clearcoat = true;
};

namespace SceneDraw
{
void useAttachments(
    RenderGraph& graph, std::span<const RenderGraph::ImageHandle> colors, RenderGraph::ImageHandle depth
);
vk::Rect2D beginRendering(
    RenderGraph&                              graph,
    vk::raii::CommandBuffer&                  commandBuffer,
    const PassFrame&                          execution,
    std::span<const RenderGraph::ImageHandle> colors,
    RenderGraph::ImageHandle                  depth,
    vk::AttachmentLoadOp                      colorLoadOp
);
void recordMeshes(
    vk::raii::CommandBuffer&    commandBuffer,
    const PassFrame&            execution,
    std::span<const DrawItem>   items,
    std::span<const vk::Format> colorFormats,
    ShaderHandle                shader  = {},
    SceneShaderOptions          options = {}
);
}  // namespace SceneDraw
