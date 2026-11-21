#pragma once

#include "core/MetadataAnnotations.h"

#include "render/graph/RenderGraph.h"

struct RL_RECORD GBufferResources
{
    RL_FIELD("diffuseOcclusion") RenderGraph::ImageHandle diffuseOcclusion;
    RL_FIELD("normalRoughness") RenderGraph::ImageHandle normalRoughness;
    RL_FIELD("emissiveSpecularF90") RenderGraph::ImageHandle emissiveSpecularF90;
    RL_FIELD("specularF0DiffuseFresnel") RenderGraph::ImageHandle specularF0DiffuseFresnel;
    RL_FIELD("sheenColorRoughness") RenderGraph::ImageHandle sheenColorRoughness;
    RL_FIELD("depth") RenderGraph::ImageHandle depth;
};

#include RL_METADATA_HEADER("metadata/src/render/raster/GBufferResources.generated.h")
