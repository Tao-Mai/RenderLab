#pragma once

#include <filesystem>

#include <nlohmann/json_fwd.hpp>

struct RendererConfig;

[[nodiscard]] nlohmann::json loadRenderGraph(const std::filesystem::path& script, const RendererConfig& config);
