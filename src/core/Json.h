#pragma once

#include "asset/AssetID.h"
#include "core/Logger.h"

#include <array>
#include <concepts>
#include <exception>
#include <filesystem>
#include <fstream>
#include <glm/gtc/quaternion.hpp>
#include <iterator>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

struct Scene;

namespace rfl
{
template <class T>
struct Reflector<AssetID<T>>
{
    struct ReflType
    {
        std::string value;
    };

    static AssetID<T> to(const ReflType& data) { return data.value; }
    static ReflType from(const AssetID<T>& id) { return {id.value}; }
};

template <glm::length_t N, class T, glm::qualifier Q>
struct Reflector<glm::vec<N, T, Q>>
{
    using ReflType = std::array<T, N>;

    static glm::vec<N, T, Q> to(const ReflType& data)
    {
        glm::vec<N, T, Q> value{};
        for (glm::length_t index = 0; index < N; ++index)
            value[index] = data[index];

        return value;
    }

    static ReflType from(const glm::vec<N, T, Q>& value)
    {
        ReflType data{};
        for (glm::length_t index = 0; index < N; ++index)
            data[index] = value[index];

        return data;
    }
};

template <glm::length_t C, glm::length_t R, class T, glm::qualifier Q>
struct Reflector<glm::mat<C, R, T, Q>>
{
    using ReflType = std::array<glm::vec<R, T, Q>, C>;

    static glm::mat<C, R, T, Q> to(const ReflType& data)
    {
        glm::mat<C, R, T, Q> value{};
        for (glm::length_t column = 0; column < C; ++column)
            value[column] = data[column];

        return value;
    }

    static ReflType from(const glm::mat<C, R, T, Q>& value)
    {
        ReflType data{};
        for (glm::length_t column = 0; column < C; ++column)
            data[column] = value[column];

        return data;
    }
};

template <class T, glm::qualifier Q>
struct Reflector<glm::qua<T, Q>>
{
    using ReflType = std::array<T, 4>;

    static glm::qua<T, Q> to(const ReflType& data) { return {data[3], data[0], data[1], data[2]}; }
    static ReflType from(const glm::qua<T, Q>& value) { return {value.x, value.y, value.z, value.w}; }
};
}

namespace asset_json
{
namespace detail
{
template <class T>
using JsonOptionalFields = std::conditional_t<std::same_as<T, Scene>, rfl::Processors<>, rfl::NoOptionals>;
}

template <class T>
[[nodiscard]] rfl::Result<T> read(std::string_view text)
{
    try
    {
        return rfl::json::read<T, rfl::NoExtraFields, detail::JsonOptionalFields<T>, rfl::EnumNamesOnly,
                               rfl::AddTagsToVariants>(text);
    }
    catch (const std::exception& error)
    {
        return rfl::error(error.what());
    }
}

template <class T>
[[nodiscard]] std::string write(const T& data, std::string_view destination, yyjson_write_flag flags = 0)
{
    try
    {
        return rfl::json::write<detail::JsonOptionalFields<T>, rfl::AddTagsToVariants>(data, flags);
    }
    catch (const std::exception& error)
    {
        LOG_FATAL("failed to encode JSON '{}': {}", destination, error.what());
    }
}

template <class T>
[[nodiscard]] T load(const std::filesystem::path& file)
{
    std::ifstream input{file, std::ios::binary};
    CHECK(input.is_open(), "failed to open JSON '{}'", file.string());

    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    CHECK(!input.bad(), "failed to read JSON '{}'", file.string());

    auto result = read<T>(text);
    CHECK(result.has_value(), "invalid JSON '{}': {}", file.string(), result.error().what());

    return std::move(*result);
}

template <class T>
void save(const std::filesystem::path& file, const T& value)
{
    const auto text = write(value, file.string(), rfl::json::pretty);

    if (file.has_parent_path())
    {
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        CHECK(!error, "failed to create JSON directory '{}': {}", file.parent_path().string(), error.message());
    }

    auto temporary = file;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        CHECK(output.is_open(), "failed to open JSON temporary file '{}'", temporary.string());
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.put('\n');
        output.close();
        CHECK(output, "failed to write JSON temporary file '{}'", temporary.string());
    }

    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    CHECK(!error, "failed to replace JSON '{}': {}", file.string(), error.message());
}
}
