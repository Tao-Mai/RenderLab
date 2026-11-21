#pragma once

#include "core/MetadataAnnotations.h"

enum class RL_ENUM RenderPath
{
    Forward RL_ENUM_VALUE("Forward"),
    Deferred RL_ENUM_VALUE("Deferred"),
    RayTracing RL_ENUM_VALUE("RayTracing"),
    OfflinePathTracing RL_ENUM_VALUE("OfflinePathTracing")
};

enum class RL_ENUM PostProcessMode
{
    RenderLab RL_ENUM_VALUE("RenderLab"),
    Falcor RL_ENUM_VALUE("Falcor"),
};

struct RL_RECORD RenderFeature
{
    RL_FIELD("ibl") bool            ibl                = true;
    RL_FIELD("ambientCoefficient") float           ambientCoefficient = 0.1f;
    RL_FIELD("hdr") bool            hdr                = true;
    RL_FIELD("postProcess") PostProcessMode postProcess        = PostProcessMode::RenderLab;
    RL_FIELD("shadows") bool            shadows            = true;
    RL_FIELD("bloom") bool            bloom              = true;
    RL_FIELD("svgf") bool            svgf               = true;
};

struct RendererConfig
{
    RenderPath    renderPath = RenderPath::Forward;
    RenderFeature feature;
};

#include RL_METADATA_HEADER("metadata/src/render/RendererConfig.generated.h")
