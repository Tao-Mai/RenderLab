//
// Created by 24500 on 2026/10/3.
//

#pragma once

#include "core/Annotations.h"
#include "core/Reflect.h"

#include <bit>
#include <filesystem>

#include <meta>
#include <charconv>
#include <concepts>
#include <optional>
#include <stdexcept>
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
    const std::string name = j.get<std::string>();

    template for (constexpr auto enumerator :
        std::define_static_array(std::meta::enumerators_of(^^T)))
    {
        if (name == std::meta::identifier_of(enumerator))
            return [:enumerator:];
    }

    throw json::other_error::create(501, "unknown enumerator '" + name + "'", &j);
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

    if (remaining != 0)
        throw json::other_error::create(501, "unknown flag bits", &result);

    return result;
}

template<class Base>
json Serialize(const std::unique_ptr<Base>& value)
{
    if (!value) return nullptr;

    const auto& names = BaseTypeInfo<Base>::typeNames;
    const auto name = names.find(typeid(*value));
    if (name == names.end())
        throw json::other_error::create(501, "unregistered polymorphic type", nullptr);

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

        throw json::other_error::create(501, "unknown enum value", nullptr);
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
    const auto& array = j.get_ref<const json::array_t&>();
    using Bits = std::make_unsigned_t<std::underlying_type_t<T>>;
    Bits result = 0;
    for (const auto& name : array)
    {
        const Bits bit = static_cast<Bits>(std::to_underlying(DeserializeEnum<T>(name)));
        if (!std::has_single_bit(bit))
            throw json::other_error::create(501, "expected a single named flag", &name);

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

    if (!j.is_object() || j.size() != 1)
        throw json::other_error::create(501, "expected exactly one polymorphic type key", &j);

    const auto entry = j.begin();
    const std::string& name = entry.key();
    const auto& types = BaseTypeInfo<Base>::baseTypeInfoMap;
    const auto type = types.find(name);
    if (type == types.end())
        throw json::other_error::create(501, "unregistered polymorphic type '" + name + "'", &j);

    if (!entry.value().is_object())
        throw json::other_error::create(501, "expected object data for polymorphic type '" + name + "'", &j);

    value = type->second.DeserializeBase(entry.value());
}

template<class T>
void Deserialize(const json& j, T& value)
{
    using U = std::remove_cvref_t<T>;

    if constexpr (std::is_arithmetic_v<U> || std::is_same_v<U, std::string>)
    {
        value = j.get<U>();
    }
    else if constexpr (std::is_same_v<U, std::filesystem::path>)
    {
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
        const auto& array = j.get_ref<const json::array_t&>();
        if (array.size() != static_cast<size_t>(U::length()))
            throw json::other_error::create(501, "invalid vector or quaternion length", &j);

        for (glm::length_t index = 0; index < U::length(); ++index)
            value[index] = array[index].template get<typename U::value_type>();
    }
    else if constexpr (IsMap<U>)
    {
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
                if (error != std::errc{} || end != name.data() + name.size())
                    throw json::other_error::create(501, "invalid map key '" + name + "'", &j);
            }

            typename U::mapped_type item{};
            Deserialize(element, item);
            result.emplace(std::move(key), std::move(item));
        }

        value = std::move(result);
    }
    else if constexpr (IsVector<U>)
    {
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

        template for (constexpr auto member :
            std::define_static_array(ReflectedDataMembers(^^U, ctx)))
        {
            const std::string name(std::meta::identifier_of(member));

            Deserialize(j.at(name), value.[:member:]);
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
    if (ownedName.empty() || types.contains(ownedName) || names.contains(typeid(T)))
        throw std::logic_error("duplicate or empty registered type name: " + ownedName);

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
