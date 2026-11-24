#pragma once

#include "render/graph/RenderGraph.h"

#include <filesystem>

struct LoadedRenderGraph
{
    ref<RenderGraph> graph;
    Properties::json defaults = Properties::json::object();
    Properties::json parameters;
};

[[nodiscard]] LoadedRenderGraph loadRenderGraph(
    rhi::IDevice& device, const std::filesystem::path& path, const std::string& graphName,
    const nlohmann::json& overrides = nlohmann::json::object()
);
