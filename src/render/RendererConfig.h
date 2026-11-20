#pragma once

enum class RenderPath
{
    Forward,
    Deferred,
    RayTracing,
    OfflinePathTracing
};

enum class PostProcessMode
{
    RenderLab,
    Falcor,
};

struct RenderFeature
{
    bool            ibl                = true;
    float           ambientCoefficient = 0.1f;
    bool            hdr                = true;
    PostProcessMode postProcess        = PostProcessMode::RenderLab;
    bool            shadows            = true;
    bool            bloom              = true;
    bool            svgf               = true;
};

struct RendererConfig
{
    RenderPath    renderPath = RenderPath::Forward;
    RenderFeature feature;
};
