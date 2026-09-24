#pragma once

#include "asset/RflReflectors.h"
#include "core/Logger.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
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
    const std::string json = rfl::json::write(value, rfl::json::pretty);
    if (file.has_parent_path())
    {
        std::filesystem::create_directories(file.parent_path());
    }
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        CHECK(output.is_open(), "failed to open JSON temporary file '{}'", temporary.string());
        output.write(json.data(), static_cast<std::streamsize>(json.size()));
        output.close();
        CHECK(output, "failed to write JSON temporary file '{}'", temporary.string());
    }
    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    CHECK(!error, "failed to replace JSON '{}': {}", file.string(), error.message());
}
}
