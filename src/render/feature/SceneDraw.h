#pragma once

#include "render/ShaderManager.h"
#include "render/feature/RenderGraph.h"
#include "render/feature/shadow/ShadowPass.h"

#include <span>

struct SceneDrawOptions
{
    bool ibl         = true;
    bool clearcoat   = true;
    bool transparent = false;
};

namespace SceneDraw
{
ShadowPass::Output
     useLightingResources(RenderGraph& graph, RenderGraph::BufferHandle lights, ShadowPass::Output shadow);
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
    std::span<const vk::Format> colorFormats,
    ShaderHandle                shader    = {},
    SceneDrawOptions            options   = {},
    std::span<const uint32_t>   drawOrder = {}
);
}  // namespace SceneDraw
