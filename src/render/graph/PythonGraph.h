#pragma once

#include "core/MetadataAnnotations.h"

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct RL_RECORD GraphParameterControl
{
    RL_FIELD("name") std::string name;
    RL_FIELD("label") std::string label;
    RL_FIELD("defaultValue") nlohmann::json defaultValue;
    RL_FIELD("value") nlohmann::json value;
    RL_FIELD("choices") std::vector<std::string> choices;
    RL_FIELD("bounds") nlohmann::json bounds;
};

[[nodiscard]] nlohmann::json loadRenderGraph(
    const std::filesystem::path& script, const nlohmann::json& parameterOverrides
);

#include RL_METADATA_HEADER("metadata/src/render/graph/PythonGraph.generated.h")
