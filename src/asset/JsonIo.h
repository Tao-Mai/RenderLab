#pragma once

#include "asset/RflReflectors.h"
#include "asset/Asset.h"
#include "asset/Serializer.h"
#include "core/Logger.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#include <rfl/json.hpp>
#include <rfl/NoExtraFields.hpp>
#include <rfl/SnakeCaseToPascalCase.hpp>

namespace asset_json
{
template <class T>
[[nodiscard]] T load(const std::filesystem::path& file)
{
    if constexpr (std::is_same_v<T, Scene::Desc>)
    {
        std::ifstream input{file};
        CHECK(input.is_open(), "failed to open scene '{}'", file.string());
        T result;
        Deserialize(json::parse(input), result);
        return result;
    }
    else
    {
        auto result = rfl::json::load<T, rfl::SnakeCaseToPascalCase,
            rfl::NoExtraFields>(file.string());
        CHECK(result, "failed to load JSON '{}': {}", file.string(), result.error().what());
        return std::move(*result);
    }
}

template <class T>
void save(const std::filesystem::path& file, const T& value)
{
    const std::string json = [&]
    {
        if constexpr (std::is_same_v<T, Scene::Desc>)
            return Serialize(value).dump(4);
        else
            return rfl::json::write<rfl::SnakeCaseToPascalCase>(value, rfl::json::pretty);
    }();
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
