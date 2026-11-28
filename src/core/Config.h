#pragma once

#include "asset/AssetID.h"
#include "render/graph/RenderPassOptions.h"

#include <filesystem>
#include <map>
#include <string>

struct Scene;
struct SceneLayer;

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

        std::map<std::string, AssetID<SceneLayer>> sceneLayerSelections;

        std::map<std::string, bool> uiExpanded;
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
