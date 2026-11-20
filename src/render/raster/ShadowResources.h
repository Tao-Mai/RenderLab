#pragma once

#include "render/graph/RenderGraph.h"

#include <span>

class FrameData;
class RenderResourceManager;

struct ShadowResources
{
    RenderGraph::ImageHandle  pointLightShadowMap;
    RenderGraph::ImageHandle  spotLightShadowMap;
    RenderGraph::ImageHandle  rectAreaLightShadowMap;
    RenderGraph::ImageHandle  cascadeShadowMap;
    RenderGraph::BufferHandle pointShadowBuffer;
    RenderGraph::BufferHandle spotShadowBuffer;
    RenderGraph::BufferHandle rectAreaShadowBuffer;
    RenderGraph::BufferHandle directionalShadowBuffer;
};

[[nodiscard]] ShadowResources createUnshadowedResources(RenderGraph& graph);
void useRasterLighting(RenderGraph& graph, RenderGraph::BufferHandle lights, const ShadowResources& shadows);
void bindShadowResources(
    RenderGraph& graph, const ShadowResources& shadows, std::span<FrameData> frames, RenderResourceManager& resources
);
