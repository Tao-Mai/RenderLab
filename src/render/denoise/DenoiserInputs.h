#pragma once

#include "core/MetadataAnnotations.h"

#include "render/graph/RenderGraph.h"

struct RL_RECORD DenoiserInputs
{
    RL_FIELD("depth") RenderGraph::ImageHandle depth;
    RL_FIELD("worldNormal") RenderGraph::ImageHandle worldNormal;
    RL_FIELD("direct") RenderGraph::ImageHandle direct;
    RL_FIELD("indirect") RenderGraph::ImageHandle indirect;
    RL_FIELD("emission") RenderGraph::ImageHandle emission;
    RL_FIELD("albedo") RenderGraph::ImageHandle albedo;
};

#include RL_METADATA_HEADER("metadata/src/render/denoise/DenoiserInputs.generated.h")
