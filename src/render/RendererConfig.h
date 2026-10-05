#pragma once

#include "asset/Asset.h"

enum class RenderPath
{
    Rasterization,
    RayTracing
};

struct RenderFeature
{
    bool  ibl                = true;
    float ambientCoefficient = 0.1f;
    struct Shadow
    {
        bool     enable             = true;
        uint32_t pointShadowMapSize = 1024;
    } shadow;
};

struct RendererConfig
{
    RenderPath                             renderPath = RenderPath::Rasterization;
    TextureBinding                         brdfLut;
    RenderFeature                          feature;
    std::optional<EnvironmentMapAsset::ID> environmentMap;
};
