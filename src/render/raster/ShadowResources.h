#pragma once

#include "core/MetadataAnnotations.h"

#include "render/graph/RenderGraph.h"

#include <span>

class FrameData;
class RenderResourceManager;

struct RL_RECORD ShadowResources
{
    RL_FIELD("pointLightShadowMap") RenderGraph::ImageHandle  pointLightShadowMap;
    RL_FIELD("spotLightShadowMap") RenderGraph::ImageHandle  spotLightShadowMap;
    RL_FIELD("rectAreaLightShadowMap") RenderGraph::ImageHandle  rectAreaLightShadowMap;
    RL_FIELD("cascadeShadowMap") RenderGraph::ImageHandle  cascadeShadowMap;
    RL_FIELD("pointShadowBuffer") RenderGraph::BufferHandle pointShadowBuffer;
    RL_FIELD("spotShadowBuffer") RenderGraph::BufferHandle spotShadowBuffer;
    RL_FIELD("rectAreaShadowBuffer") RenderGraph::BufferHandle rectAreaShadowBuffer;
    RL_FIELD("directionalShadowBuffer") RenderGraph::BufferHandle directionalShadowBuffer;
};

[[nodiscard]] ShadowResources createUnshadowedResources(RenderGraph& graph);
void useRasterLighting(RenderGraph& graph, RenderGraph::BufferHandle lights, const ShadowResources& shadows);
void bindShadowResources(
    RenderGraph& graph, const ShadowResources& shadows, std::span<FrameData> frames, RenderResourceManager& resources
);

#include RL_METADATA_HEADER("metadata/src/render/raster/ShadowResources.generated.h")
