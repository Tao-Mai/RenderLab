#pragma once

#include "asset/rfl_reflectors.h"
#include "logger.h"

#include <filesystem>
#include <utility>

#include <rfl/json.hpp>

namespace asset_json
{
template <class T>
[[nodiscard]] T load(const std::filesystem::path& file)
{
    auto result = rfl::json::load<T>(file.string());
    CHECK(result, "failed to load JSON '{}': {}", file.string(), result.error().what());
    return std::move(*result);
}

template <class T>
void save(const std::filesystem::path& file, const T& value)
{
    if (file.has_parent_path())
    {
        std::filesystem::create_directories(file.parent_path());
    }
    auto result = rfl::json::save(file.string(), value, rfl::json::pretty);
    CHECK(result, "failed to save JSON '{}': {}", file.string(), result.error().what());
}
}
