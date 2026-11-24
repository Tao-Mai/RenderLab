#pragma once

#include "render/graph/RenderGraph.h"

#include <filesystem>
#include <memory>

struct LoadedRenderGraph
{
    std::shared_ptr<RenderGraph> graph;
    nlohmann::ordered_json       defaults = nlohmann::ordered_json::object();
    nlohmann::ordered_json       parameters;
};

[[nodiscard]] LoadedRenderGraph loadRenderGraph(
    rhi::IDevice& device, const std::filesystem::path& path, const std::string& graphName,
    const nlohmann::json& overrides = nlohmann::json::object()
);
