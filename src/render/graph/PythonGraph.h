#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct GraphParameterControl
{
    std::string name;
    std::string label;
    nlohmann::json defaultValue;
    nlohmann::json value;
    std::vector<std::string> choices;
    nlohmann::json bounds;
};

[[nodiscard]] nlohmann::json loadRenderGraph(
    const std::filesystem::path& script, const nlohmann::json& parameterOverrides
);
