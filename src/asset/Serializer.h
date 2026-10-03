//
// Created by 24500 on 2026/10/3.
//

#pragma once

#include "core/Annotations.h"
#include "core/Reflect.h"
#include "core/Logger.h"

#include <bit>
#include <filesystem>

#include <meta>
#include <charconv>
#include <concepts>
#include <optional>
#include <vector>
#include <string>
#include <type_traits>
#include <utility>
#include <nlohmann/json.hpp>
#include <glm/gtc/quaternion.hpp>

using json = nlohmann::json;

template<class T>
T DeserializeEnum(const json& j)
{
    CHECK(j.is_string(), "expected an enum name");
    const std::string name = j.get<std::string>();

    template for (constexpr auto enumerator :
        std::define_static_array(std::meta::enumerators_of(^^T)))
    {
        if (name == std::meta::identifier_of(enumerator))
            return [:enumerator:];
    }

    CHECK(false, "unknown enumerator '{}'", name);
}

template<FlagEnum T>
json Serialize(const T& value)
{
    json result = json::array();
    using Bits = std::make_unsigned_t<std::underlying_type_t<T>>;
    Bits remaining = static_cast<Bits>(std::to_underlying(value));

    template for (constexpr auto enumerator :
        std::define_static_array(std::meta::enumerators_of(^^T)))
    {
        constexpr Bits bit = static_cast<Bits>(std::to_underlying([:enumerator:]));
        if constexpr (std::has_single_bit(bit))
        {
            if ((remaining & bit) != 0)
            {
                result.push_back(std::string(std::meta::identifier_of(enumerator)));
                remaining &= ~bit;
            }
        }
    }

    CHECK(remaining == 0, "unknown flag bits");

    return result;
}

template<class Base>
json Serialize(const std::unique_ptr<Base>& value)
{
    if (!value) return nullptr;

    const auto& names = BaseTypeInfo<Base>::typeNames;
    const auto name = names.find(typeid(*value));
    DCHECK(name != names.end(), "unregistered polymorphic type");

    const auto& type = BaseTypeInfo<Base>::baseTypeInfoMap.at(name->second);
    return json{{name->second, type.SerializeBase(*value)}};
}

template<class T>
json Serialize(const T& value)
{
    using U = std::remove_cvref_t<T>;

    if constexpr (std::is_arithmetic_v<U> || std::is_same_v<U, std::string>)
    {
        return value;
    }
    else if constexpr (std::is_same_v<U, std::filesystem::path>)
    {
        return value.generic_string();
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
        template for (constexpr auto enumerator :
            std::define_static_array(std::meta::enumerators_of(^^U)))
        {
            if (value == [:enumerator:])
                return std::string(std::meta::identifier_of(enumerator));
        }

        CHECK(false, "unknown enum value");
    }
    else
    {
        json result = json::object();
        constexpr auto ctx = std::meta::access_context::current();

        template for (constexpr auto member :
            std::define_static_array(ReflectedDataMembers(^^U, ctx)))
        {
            result[std::string(std::meta::identifier_of(member))] =
                Serialize(value.[:member:]);
        }

        return result;
    }
}

template<FlagEnum T>
void Deserialize(const json& j, T& value)
{
    CHECK(j.is_array(), "expected a flag array");
    const auto& array = j.get_ref<const json::array_t&>();
    using Bits = std::make_unsigned_t<std::underlying_type_t<T>>;
    Bits result = 0;
    for (const auto& name : array)
    {
        const Bits bit = static_cast<Bits>(std::to_underlying(DeserializeEnum<T>(name)));
        CHECK(std::has_single_bit(bit), "expected a single named flag");

        result |= bit;
    }

    value = static_cast<T>(result);
}

template<class Base>
void Deserialize(const json& j, std::unique_ptr<Base>& value)
{
    if (j.is_null())
    {
        value.reset();
        return;
    }

    CHECK(j.is_object() && j.size() == 1, "expected exactly one polymorphic type key");

    const auto entry = j.begin();
    const std::string& name = entry.key();
    const auto& types = BaseTypeInfo<Base>::baseTypeInfoMap;
    const auto type = types.find(name);
    CHECK(type != types.end(), "unregistered polymorphic type '{}'", name);

    CHECK(entry.value().is_object(), "expected object data for polymorphic type '{}'", name);

    value = type->second.DeserializeBase(entry.value());
}

template<class T>
void Deserialize(const json& j, T& value)
{
    using U = std::remove_cvref_t<T>;

    if constexpr (std::is_arithmetic_v<U> || std::is_same_v<U, std::string>)
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
        U result;
        for (const auto& [name, element] : object)
        {
            typename U::key_type key{};
            if constexpr (std::is_same_v<typename U::key_type, std::string>)
                key = name;
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
        U result;
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
        constexpr auto ctx = std::meta::access_context::current();
        static constexpr auto members = std::define_static_array(ReflectedDataMembers(^^U, ctx));

        CHECK(j.is_object(), "expected an object");
        const auto& object = j.get_ref<const json::object_t&>();
        if constexpr (!HasInheritedAnnotation(^^U, ^^PartialSerialize, ctx))
        {
            for (const auto& [name, element] : object)
            {
                bool known = false;
                template for (constexpr auto member : members)
                {
                    known |= name == std::meta::identifier_of(member);
                }
                CHECK(known, "unknown field '{}'", name);
            }
        }

        template for (constexpr auto member : members)
        {
            const std::string name(std::meta::identifier_of(member));

            const auto field = j.find(name);
            CHECK(field != j.end(), "missing field '{}'", name);
            Deserialize(*field, value.[:member:]);
        }
    }
}

template<class T, class Base>
void RegisterBase(std::string_view name)
{
    static_assert(std::derived_from<T, Base> || std::same_as<T, Base>);
    static_assert(std::has_virtual_destructor_v<Base>);
    static_assert(!std::is_abstract_v<T>);

    auto& types = BaseTypeInfo<Base>::baseTypeInfoMap;
    auto& names = BaseTypeInfo<Base>::typeNames;
    const std::string ownedName{name};
    DCHECK(!ownedName.empty() && !types.contains(ownedName) && !names.contains(typeid(T)),
          "duplicate or empty registered type name: {}", ownedName);

    types.emplace(ownedName, BaseTypeInfo<Base>{
        .DeserializeBase = [](const json& data) -> std::unique_ptr<Base>
        {
            auto result = std::make_unique<T>();
            Deserialize(data, *result);
            return result;
        },
        .SerializeBase = [](const Base& value)
        {
            return Serialize(static_cast<const T&>(value));
        },
        .IterateMembers = [](Base& value, const std::function<void(MemberRef)>& visitor)
        {
            auto& object = static_cast<T&>(value);
            constexpr auto ctx = std::meta::access_context::current();
            template for (constexpr auto member :
                std::define_static_array(ReflectedDataMembers(^^T, ctx)))
            {
                using MemberType = std::remove_cvref_t<decltype(object.[:member:])>;
                visitor({std::meta::identifier_of(member), typeid(MemberType), &object.[:member:]});
            }
        },
    });
    names.emplace(typeid(T), ownedName);
}
