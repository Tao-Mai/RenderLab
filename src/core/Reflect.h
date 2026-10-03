#pragma once

#include "core/Annotations.h"

#include <functional>
#include <memory>
#include <meta>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <glm/fwd.hpp>
#include <nlohmann/json.hpp>

template <class T>
inline constexpr bool IsVector = false;

template <class T, class Allocator>
inline constexpr bool IsVector<std::vector<T, Allocator>> = true;

template <class T>
inline constexpr bool IsOptional = false;

template <class T>
inline constexpr bool IsOptional<std::optional<T>> = true;

template <class T>
inline constexpr bool IsMap = false;

template <class Key, class Value, class Hash, class Equal, class Allocator>
inline constexpr bool IsMap<std::unordered_map<Key, Value, Hash, Equal, Allocator>> = true;

template <class T>
inline constexpr bool IsGlmVector = false;

template <glm::length_t N, class T, glm::qualifier Q>
inline constexpr bool IsGlmVector<glm::vec<N, T, Q>> = true;

template <class T>
concept FlagEnum = std::is_enum_v<T> &&
    (!std::meta::annotations_of_with_type(std::meta::dealias(^^T), ^^Flags).empty());

consteval std::vector<std::meta::info>
AllDataMembers(std::meta::info type, std::meta::access_context ctx)
{
    std::vector<std::meta::info> result;
    for (auto base : std::meta::bases_of(type, ctx))
    {
        auto members = AllDataMembers(std::meta::type_of(base), ctx);
        result.insert(result.end(), members.begin(), members.end());
    }

    for (auto member : std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked()))
    {
        if (std::meta::is_accessible(member, ctx))
            result.push_back(member);
    }

    return result;
}

consteval bool HasInheritedAnnotation(std::meta::info           type, std::meta::info annotation,
                                      std::meta::access_context ctx)
{
    type = std::meta::dealias(type);
    if (!std::meta::annotations_of_with_type(type, annotation).empty())
        return true;

    for (auto base : std::meta::bases_of(type, ctx))
        if (HasInheritedAnnotation(std::meta::type_of(base), annotation, ctx))
            return true;

    return false;
}

consteval std::vector<std::meta::info>
ReflectedDataMembers(std::meta::info type, std::meta::access_context ctx)
{
    auto members = AllDataMembers(type, ctx);
    if (!HasInheritedAnnotation(type, ^^PartialSerialize, ctx))
        return members;

    std::vector<std::meta::info> result;
    for (auto member : members)
        if (!std::meta::annotations_of_with_type(member, ^^ReflectField).empty())
            result.push_back(member);

    return result;
}

struct MemberRef
{
    std::string_view name;
    std::type_index  type;
    void*            value;
};

template <class Base>
struct BaseTypeInfo
{
    std::unique_ptr<Base> (*DeserializeBase)(const nlohmann::json&);
    nlohmann::json (*       SerializeBase)(const Base&);
    void (*                 IterateMembers)(Base&, const std::function<void(MemberRef)>&);

    inline static std::unordered_map<std::string, BaseTypeInfo>    baseTypeInfoMap;
    inline static std::unordered_map<std::type_index, std::string> typeNames;
};

// Defined alongside the serialization templates in core/Serializer.h.
template <class T, class Base>
void RegisterBase(std::string_view name);
