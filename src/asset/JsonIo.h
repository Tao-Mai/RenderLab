#pragma once

#include "asset/Serializer.h"
#include "core/Logger.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace asset_json
{
template <class T>
[[nodiscard]] T load(const std::filesystem::path& file)
{
    std::ifstream input{file, std::ios::binary};
    CHECK(input.is_open(), "failed to open JSON '{}'", file.string());

    T result{};
    const auto data = json::parse(input, nullptr, false);
    CHECK(!data.is_discarded(), "invalid JSON '{}'", file.string());
    Deserialize(data, result);
    return result;
}

template <class T>
void save(const std::filesystem::path& file, const T& value)
{
    const std::string serialized = Serialize(value).dump(4);

    if (file.has_parent_path())
    {
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        CHECK(!error, "failed to create JSON directory '{}': {}", file.parent_path().string(), error.message());
    }
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        CHECK(output.is_open(), "failed to open JSON temporary file '{}'", temporary.string());
        output.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        output.close();
        CHECK(output, "failed to write JSON temporary file '{}'", temporary.string());
    }
    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    CHECK(!error, "failed to replace JSON '{}': {}", file.string(), error.message());
}
}
