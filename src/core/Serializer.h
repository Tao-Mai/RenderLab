//
// Created by 24500 on 2026/10/3.
//

#pragma once

#include "core/Logger.h"
#include "core/Metadata.h"
#include "core/TypeRegistry.h"
#include "core/TypeTraits.h"

#include <bit>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <charconv>
#include <concepts>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using json = nlohmann::json;

template <class T>
T DeserializeEnum(const json& j)
{
    CHECK(j.is_string(), "expected an enum name");
    const std::string name = j.get<std::string>();

    for (const auto& entry : metadata::Enum<T>::kValues)
        if (name == entry.name)
            return entry.value;

    CHECK(false, "unknown enumerator '{}'", name);
}

template <metadata::FlagEnum T>
json Serialize(const T& value)
{
    json result    = json::array();
    using Bits     = std::make_unsigned_t<std::underlying_type_t<T>>;
    Bits remaining = static_cast<Bits>(std::to_underlying(value));

    for (const auto& entry : metadata::Enum<T>::kValues)
    {
        const Bits bit = static_cast<Bits>(std::to_underlying(entry.value));
        if (std::has_single_bit(bit) && (remaining & bit) != 0)
        {
            result.push_back(entry.name);
            remaining &= ~bit;
        }
    }

    CHECK(remaining == 0, "unknown flag bits");

    return result;
}

template <class Base>
json Serialize(const std::unique_ptr<Base>& value)
{
    if (!value)
        return nullptr;

    const auto& types = TypeRegistry<Base>::get().nativeTypes;
    const auto  type  = types.find(typeid(*value));


    return json{{type->second->id, type->second->serialize(*value)}};
}

template <class T>
json Serialize(const T& value)
{
    using U = std::remove_cvref_t<T>;

    if constexpr (std::is_arithmetic_v<U> || std::is_same_v<U, std::string> || std::is_same_v<U, json>)
    {
        return value;
    }
    else if constexpr (std::is_same_v<U, std::filesystem::path>)
    {
        return value.generic_string();
    }
    else if constexpr (IsVariant<U>)
    {
        return std::visit(
            []<class Alternative>(const Alternative& data)
            { return json{{metadata::Schema<Alternative>::kTypeId, Serialize(data)}}; },
            value
        );
    }
    else if constexpr (IsOptional<U>)
    {
        return value ? Serialize(*value) : json(nullptr);
    }
    else if constexpr (IsGlmVector<U>)
    {
        json result = json::array();
        for (glm::length_t index = 0; index < U::length(); ++index)
            result.push_back(value[index]);

        return result;
    }
    else if constexpr (kIsGlmMatrix<U>)
    {
        json result = json::array();
        for (glm::length_t column = 0; column < U::length(); ++column)
            result.push_back(Serialize(value[column]));
        return result;
    }
    else if constexpr (std::is_same_v<U, glm::quat>)
    {
        return json::array({value.x, value.y, value.z, value.w});
    }
    else if constexpr (IsMap<U>)
    {
        json result = json::object();
        for (const auto& [key, item] : value)
        {
            if constexpr (std::is_same_v<typename U::key_type, std::string>)
                result[key] = Serialize(item);
            else if constexpr (std::is_enum_v<typename U::key_type>)
                result[Serialize(key).template get<std::string>()] = Serialize(item);
            else
                result[std::to_string(key)] = Serialize(item);
        }

        return result;
    }
    else if constexpr (IsVector<U>)
    {
        json result = json::array();
        for (const auto& element : value)
            result.push_back(Serialize(element));

        return result;
    }
    else if constexpr (std::is_enum_v<U>)
    {
        for (const auto& entry : metadata::Enum<U>::kValues)
            if (value == entry.value)
                return entry.name;

        CHECK(false, "unknown enum value");
    }
    else
    {
        json result = json::object();
        metadata::visitFields(
            value,
            [&](const auto& field, const auto& member) { result[field.name] = Serialize(member); }
        );

        return result;
    }
}

template <metadata::FlagEnum T>
void Deserialize(const json& j, T& value)
{
    CHECK(j.is_array(), "expected a flag array");
    const auto& array = j.get_ref<const json::array_t&>();
    using Bits        = std::make_unsigned_t<std::underlying_type_t<T>>;
    Bits result       = 0;
    for (const auto& name : array)
    {
        const Bits bit = static_cast<Bits>(std::to_underlying(DeserializeEnum<T>(name)));
        CHECK(std::has_single_bit(bit), "expected a single named flag");

        result |= bit;
    }

    value = static_cast<T>(result);
}

template <class Base>
void Deserialize(const json& j, std::unique_ptr<Base>& value)
{
    if (j.is_null())
    {
        value.reset();
        return;
    }

    CHECK(j.is_object() && j.size() == 1, "expected exactly one polymorphic type key");

    const auto         entry = j.begin();
    const std::string& name  = entry.key();
    const auto&        types = TypeRegistry<Base>::get().entries;
    const auto         type  = types.find(name);
    CHECK(type != types.end(), "unregistered polymorphic type '{}'", name);

    CHECK(entry.value().is_object(), "expected object data for polymorphic type '{}'", name);

    value = type->second.deserialize(entry.value());
}

template <class T>
void Deserialize(const json& j, T& value)
{
    using U = std::remove_cvref_t<T>;

    if constexpr (std::is_same_v<U, json>)
    {
        value = j;
    }
    else if constexpr (std::is_arithmetic_v<U> || std::is_same_v<U, std::string>)
    {
        if constexpr (std::same_as<U, std::string>)
            CHECK(j.is_string(), "expected a string");
        else if constexpr (std::same_as<U, bool>)
            CHECK(j.is_boolean(), "expected a boolean");
        else
            CHECK(j.is_number(), "expected a number");

        value = j.get<U>();
    }
    else if constexpr (std::is_same_v<U, std::filesystem::path>)
    {
        CHECK(j.is_string(), "expected a path string");
        value = j.get<std::string>();
    }
    else if constexpr (IsVariant<U>)
    {
        CHECK(j.is_object() && j.size() == 1, "expected exactly one variant type key");
        const auto entry   = j.begin();
        const bool matched = [&]<class... Alternatives>(std::variant<Alternatives...>& target)
        {
            const auto read = [&]<class Alternative>
            {
                if (entry.key() != metadata::Schema<Alternative>::kTypeId)
                    return false;

                Alternative data{};
                Deserialize(entry.value(), data);
                target = std::move(data);
                return true;
            };
            return (read.template operator()<Alternatives>() || ...);
        }(value);
        CHECK(matched, "unknown variant type '{}'", entry.key());
    }
    else if constexpr (IsOptional<U>)
    {
        if (j.is_null())
            value.reset();
        else
        {
            typename U::value_type item{};
            Deserialize(j, item);
            value = std::move(item);
        }
    }
    else if constexpr (kIsGlmMatrix<U>)
    {
        CHECK(j.is_array() && j.size() == static_cast<size_t>(U::length()));
        for (glm::length_t column = 0; column < U::length(); ++column)
            Deserialize(j[column], value[column]);
    }
    else if constexpr (IsGlmVector<U> || std::is_same_v<U, glm::quat>)
    {
        CHECK(j.is_array(), "expected a vector or quaternion array");
        const auto& array = j.get_ref<const json::array_t&>();
        CHECK(array.size() == static_cast<size_t>(U::length()), "invalid vector or quaternion length");

        for (glm::length_t index = 0; index < U::length(); ++index)
            Deserialize(array[index], value[index]);
    }
    else if constexpr (IsMap<U>)
    {
        CHECK(j.is_object(), "expected a map object");
        const auto& object = j.get_ref<const json::object_t&>();
        U           result;
        for (const auto& [name, element] : object)
        {
            typename U::key_type key{};
            if constexpr (std::is_same_v<typename U::key_type, std::string>)
                key = name;
            else if constexpr (std::is_enum_v<typename U::key_type>)
                Deserialize(json(name), key);
            else
            {
                const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), key);
                CHECK(error == std::errc{} && end == name.data() + name.size(), "invalid map key '{}'", name);
            }

            typename U::mapped_type item{};
            Deserialize(element, item);
            result.emplace(std::move(key), std::move(item));
        }

        value = std::move(result);
    }
    else if constexpr (IsVector<U>)
    {
        CHECK(j.is_array(), "expected an array");
        const auto& array = j.get_ref<const json::array_t&>();
        U           result;
        result.reserve(array.size());
        for (const auto& element : array)
        {
            typename U::value_type item{};
            Deserialize(element, item);
            result.push_back(std::move(item));
        }

        value = std::move(result);
    }
    else if constexpr (std::is_enum_v<U>)
    {
        value = DeserializeEnum<U>(j);
    }
    else
    {
        CHECK(j.is_object(), "expected an object");
        metadata::visitFields(
            value,
            [&](const auto& field, auto& member)
            {
                const auto item = j.find(field.name);
                CHECK(item != j.end(), "missing field '{}'", field.name);
                Deserialize(*item, member);
            }
        );

        if constexpr (!metadata::Schema<U>::kAllowUnknownFields)
        {
            constexpr auto kFieldCount = std::tuple_size_v<decltype(metadata::Schema<U>::kFields)>;
            CHECK(j.size() == kFieldCount, "unexpected object fields, expected {}, got {}", kFieldCount, j.size());
        }
    }
}

template <class T, class Base>
void RegisterType()
{
    static_assert(std::derived_from<T, Base>);
    static_assert(std::has_virtual_destructor_v<Base>);
    static_assert(!std::is_abstract_v<T>);

    auto&          registry       = TypeRegistry<Base>::get();
    constexpr auto kId            = metadata::Schema<T>::kTypeId;
    using Parent                  = typename metadata::Schema<T>::Parent;
    const std::string_view parent = []() -> std::string_view
    {
        if constexpr (std::is_void_v<Parent>)
            return {};
        else
            return metadata::Schema<Parent>::kTypeId;
    }();


    const auto entry =
        registry.entries
            .emplace(
                kId,
                typename TypeRegistry<Base>::Entry{
                    .id          = kId,
                    .parent      = parent,
                    .create      = []() -> std::unique_ptr<Base> { return std::make_unique<T>(); },
                    .serialize   = [](const Base& value) { return Serialize(static_cast<const T&>(value)); },
                    .deserialize = [](const json& data) -> std::unique_ptr<Base>
                    {
                        auto result = std::make_unique<T>();
                        Deserialize(data, *result);
                        return result;
                    },
                }
            )
            .first;
    registry.nativeTypes.emplace(typeid(T), &entry->second);
}

namespace asset_json
{
template <class T>
[[nodiscard]] T load(const std::filesystem::path& file)
{
    std::ifstream input{file, std::ios::binary};
    CHECK(input.is_open(), "failed to open JSON '{}'", file.string());

    T          result{};
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
}  // namespace asset_json
