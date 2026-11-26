#pragma once

#include "asset/AssetID.h"
#include "render/graph/RenderPassOptions.h"

#include <filesystem>
#include <string>

struct Scene;

enum class RenderGraphType
{
    Minimal,
    Restir,
};

enum class PostProcessPipeline
{
    RenderLab,
    Falcor,
};

struct RenderGraphSettings
{
    bool accumulate = true;

    bool hdr = true;

    PostProcessPipeline postProcess = PostProcessPipeline::RenderLab;

    bool bloom = true;

    bool operator==(const RenderGraphSettings&) const = default;
};

struct Config
{
    struct Editor
    {
        int width = 0;

        int height = 0;

        std::string title;

        bool showSceneObjects = false;
    };

    struct Renderer
    {
        RenderGraphType graph;

        RenderGraphSettings parameters;
        RenderPassOptions passes;
    };

    std::filesystem::path assets;

    AssetID<Scene> initialScene;

    Editor editor;

    Renderer renderer;
};
