#pragma once

#include "asset/asset_desc.h"
#include "core/logger.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <entt/core/hashed_string.hpp>
#include <entt/core/type_info.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <rfl.hpp>
#include <rfl/Generic.hpp>
#include <rfl/from_generic.hpp>
#include <rfl/parsing/Parser_base.hpp>
#include <rfl/to_generic.hpp>

namespace rfl_detail
{
template <class T>
[[nodiscard]] inline const T& metaAnyAs(const entt::meta_any& value)
{
    const T* typed = value.try_cast<T>();
    CHECK(typed != nullptr, "meta_any does not hold expected field type");
    return *typed;
}

[[nodiscard]] inline std::string metaKeyToString(const entt::meta_any& key)
{
    if (const auto* asString = key.try_cast<std::string>())
    {
        return *asString;
    }
    if (key.allow_cast<std::int64_t>())
    {
        return std::to_string(key.cast<std::int64_t>());
    }
    if (key.allow_cast<int>())
    {
        return std::to_string(key.cast<int>());
    }
    CHECK(false, "unsupported associative container key type");
    return {};
}

[[nodiscard]] inline rfl::Generic metaAnyToGeneric(const entt::meta_any& value)
{
    if (!value)
    {
        return {};
    }

    const entt::meta_type type = value.type();
    if (type == entt::resolve<bool>())
    {
        return rfl::Generic{metaAnyAs<bool>(value)};
    }
    if (type == entt::resolve<std::int64_t>())
    {
        return rfl::Generic{metaAnyAs<std::int64_t>(value)};
    }
    if (type == entt::resolve<int>())
    {
        return rfl::Generic{static_cast<std::int64_t>(metaAnyAs<int>(value))};
    }
    if (type == entt::resolve<std::uint32_t>())
    {
        return rfl::Generic{static_cast<std::int64_t>(metaAnyAs<std::uint32_t>(value))};
    }
    if (type == entt::resolve<float>())
    {
        return rfl::Generic{static_cast<double>(metaAnyAs<float>(value))};
    }
    if (type == entt::resolve<double>())
    {
        return rfl::Generic{metaAnyAs<double>(value)};
    }
    if (type == entt::resolve<std::string>())
    {
        return rfl::Generic{metaAnyAs<std::string>(value)};
    }
    if (type == entt::resolve<glm::vec2>())
    {
        return rfl::to_generic(metaAnyAs<glm::vec2>(value));
    }
    if (type == entt::resolve<glm::vec3>())
    {
        return rfl::to_generic(metaAnyAs<glm::vec3>(value));
    }
    if (type == entt::resolve<glm::vec4>())
    {
        return rfl::to_generic(metaAnyAs<glm::vec4>(value));
    }
    if (type == entt::resolve<glm::quat>())
    {
        return rfl::to_generic(metaAnyAs<glm::quat>(value));
    }
    if (type.is_enum())
    {
        for (auto&& [id, data] : type.data())
        {
            (void)id;
            if (data.get({}) == value)
            {
                const char* name = data.name();
                CHECK(name != nullptr, "enum constant missing name");
                return rfl::Generic{std::string{name}};
            }
        }
        CHECK(false, "unknown enumerator for meta enum serialization");
    }

    if (const MaterialDesc* material = value.try_cast<MaterialDesc>())
    {
        return rfl::to_generic(*material);
    }

    if (auto seq = value.as_sequence_container(); seq)
    {
        rfl::Generic::Array array;
        array.reserve(seq.size());
        for (auto&& element : seq)
        {
            array.push_back(metaAnyToGeneric(element));
        }
        return rfl::Generic{std::move(array)};
    }

    if (auto assoc = value.as_associative_container(); assoc)
    {
        rfl::Generic::Object object;
        for (auto&& [key, mapped] : assoc)
        {
            object[metaKeyToString(key)] = metaAnyToGeneric(mapped);
        }
        return rfl::Generic{std::move(object)};
    }

    rfl::Generic::Object object;
    bool hasFields = false;
    for (auto&& [id, data] : type.data())
    {
        (void)id;
        hasFields = true;
        const char* name = data.name();
        CHECK(name != nullptr, "reflected field missing name");
        object[name] = metaAnyToGeneric(data.get(value));
    }
    CHECK(hasFields, "unsupported meta value type for component map serialization");
    return rfl::Generic{std::move(object)};
}

[[nodiscard]] inline entt::meta_any genericToMetaAny(
    const rfl::Generic& value, const entt::meta_type& expected)
{
    CHECK(static_cast<bool>(expected), "invalid meta type for component map field");

    if (expected == entt::resolve<bool>())
    {
        const auto parsed = value.to_bool();
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{*parsed};
    }
    if (expected == entt::resolve<int>())
    {
        if (const auto parsed = value.to_int())
        {
            return entt::meta_any{static_cast<int>(*parsed)};
        }
        const auto asString = value.to_string();
        CHECK(asString, "{}", asString.error().what());
        return entt::meta_any{std::stoi(*asString)};
    }
    if (expected == entt::resolve<std::int64_t>())
    {
        if (const auto parsed = value.to_int64())
        {
            return entt::meta_any{*parsed};
        }
        const auto asString = value.to_string();
        CHECK(asString, "{}", asString.error().what());
        return entt::meta_any{static_cast<std::int64_t>(std::stoll(*asString))};
    }
    if (expected == entt::resolve<std::uint32_t>())
    {
        if (const auto parsed = value.to_int())
        {
            return entt::meta_any{static_cast<std::uint32_t>(*parsed)};
        }
        const auto asString = value.to_string();
        CHECK(asString, "{}", asString.error().what());
        return entt::meta_any{static_cast<std::uint32_t>(std::stoul(*asString))};
    }
    if (expected == entt::resolve<float>())
    {
        const auto parsed = value.to_double();
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{static_cast<float>(*parsed)};
    }
    if (expected == entt::resolve<double>())
    {
        const auto parsed = value.to_double();
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{*parsed};
    }
    if (expected == entt::resolve<std::string>())
    {
        const auto parsed = value.to_string();
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{*parsed};
    }
    if (expected == entt::resolve<glm::vec2>())
    {
        const auto parsed = rfl::from_generic<glm::vec2>(value);
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{*parsed};
    }
    if (expected == entt::resolve<glm::vec3>())
    {
        const auto parsed = rfl::from_generic<glm::vec3>(value);
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{*parsed};
    }
    if (expected == entt::resolve<glm::vec4>())
    {
        const auto parsed = rfl::from_generic<glm::vec4>(value);
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{*parsed};
    }
    if (expected == entt::resolve<glm::quat>())
    {
        const auto parsed = rfl::from_generic<glm::quat>(value);
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{*parsed};
    }
    if (expected.is_enum())
    {
        const auto asString = value.to_string();
        CHECK(asString, "{}", asString.error().what());
        const entt::meta_data data =
            expected.data(entt::hashed_string{asString->c_str()});
        CHECK(data, "unknown enumerator '{}'", *asString);
        return data.get({});
    }

    if (expected.info() == entt::type_id<MaterialDesc>())
    {
        const auto parsed = rfl::from_generic<MaterialDesc>(value);
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{*parsed};
    }

    if (expected.is_sequence_container())
    {
        entt::meta_any instance = expected.construct();
        CHECK(instance, "failed to construct sequence container");
        auto seq = instance.as_sequence_container();
        CHECK(seq, "sequence container view unavailable");

        const auto array = value.to_array();
        CHECK(array, "{}", array.error().what());
        for (const auto& element : *array)
        {
            CHECK(
                seq.insert(seq.end(), genericToMetaAny(element, seq.value_type())),
                "failed to insert sequence element");
        }
        return instance;
    }

    if (expected.is_associative_container())
    {
        entt::meta_any instance = expected.construct();
        CHECK(instance, "failed to construct associative container");
        auto assoc = instance.as_associative_container();
        CHECK(assoc, "associative container view unavailable");

        const auto object = value.to_object();
        CHECK(object, "{}", object.error().what());
        for (const auto& [key, mapped] : *object)
        {
            const entt::meta_any keyAny = genericToMetaAny(rfl::Generic{key}, assoc.key_type());
            const entt::meta_any mappedAny = genericToMetaAny(mapped, assoc.mapped_type());
            CHECK(assoc.insert(keyAny, mappedAny), "failed to insert map entry");
        }
        return instance;
    }

    const auto object = value.to_object();
    CHECK(object, "{}", object.error().what());

    entt::meta_any instance = expected.construct();
    CHECK(instance, "failed to construct component '{}'",
        expected.name() != nullptr ? expected.name() : "<unnamed>");

    for (const auto& [fieldName, fieldValue] : *object)
    {
        const entt::meta_data data =
            expected.data(entt::hashed_string{fieldName.c_str()});
        CHECK(data, "unknown field '{}' on component '{}'", fieldName,
            expected.name() != nullptr ? expected.name() : "<unnamed>");
        const entt::meta_any fieldAny = genericToMetaAny(fieldValue, data.type());
        CHECK(data.set(instance, fieldAny), "failed to set field '{}'", fieldName);
    }
    return instance;
}
}

namespace rfl
{
template <>
struct Reflector<glm::vec2>
{
    using ReflType = std::array<float, 2>;

    static glm::vec2 to(const ReflType& value) noexcept
    {
        return {value[0], value[1]};
    }

    static ReflType from(const glm::vec2& value)
    {
        return {value.x, value.y};
    }
};

template <>
struct Reflector<glm::vec3>
{
    using ReflType = std::array<float, 3>;

    static glm::vec3 to(const ReflType& value) noexcept
    {
        return {value[0], value[1], value[2]};
    }

    static ReflType from(const glm::vec3& value)
    {
        return {value.x, value.y, value.z};
    }
};

template <>
struct Reflector<glm::vec4>
{
    using ReflType = std::array<float, 4>;

    static glm::vec4 to(const ReflType& value) noexcept
    {
        return {value[0], value[1], value[2], value[3]};
    }

    static ReflType from(const glm::vec4& value)
    {
        return {value.x, value.y, value.z, value.w};
    }
};

template <>
struct Reflector<glm::quat>
{
    using ReflType = std::array<float, 4>;

    static glm::quat to(const ReflType& value) noexcept
    {
        return {value[3], value[0], value[1], value[2]};
    }

    static ReflType from(const glm::quat& value)
    {
        return {value.x, value.y, value.z, value.w};
    }
};

template <class Hash, class KeyEqual, class Allocator>
struct Reflector<std::unordered_map<std::string, entt::meta_any, Hash, KeyEqual, Allocator>>
{
    using T = std::unordered_map<std::string, entt::meta_any, Hash, KeyEqual, Allocator>;
    using ReflType = std::unordered_map<std::string, Generic>;

    static T to(const ReflType& value)
    {
        T components;
        components.reserve(value.size());
        for (const auto& [typeName, componentValue] : value)
        {
            const entt::meta_type type =
                entt::resolve(entt::hashed_string{typeName.c_str()});
            CHECK(type, "unknown component type '{}'", typeName);
            components.emplace(
                typeName, rfl_detail::genericToMetaAny(componentValue, type));
        }
        return components;
    }

    static ReflType from(const T& value)
    {
        ReflType reflected;
        reflected.reserve(value.size());
        for (const auto& [typeName, component] : value)
        {
            CHECK(component, "null component '{}'", typeName);
            reflected.emplace(typeName, rfl_detail::metaAnyToGeneric(component));
        }
        return reflected;
    }
};

namespace parsing
{
template <class R, class W, class Hash, class KeyEqual, class Allocator, class ProcessorsType>
struct Parser<R, W,
    std::unordered_map<std::string, entt::meta_any, Hash, KeyEqual, Allocator>,
    ProcessorsType>
{
    using T = std::unordered_map<std::string, entt::meta_any, Hash, KeyEqual, Allocator>;
    using InputVarType = typename R::InputVarType;
    using ReflType = typename Reflector<T>::ReflType;

    static Result<T> read(const R& reader, const InputVarType& var) noexcept
    {
        return Parser<R, W, ReflType, ProcessorsType>::read(reader, var)
            .transform([](auto&& reflected) {
                return Reflector<T>::to(std::forward<decltype(reflected)>(reflected));
            });
    }

    template <class Parent>
    static void write(const W& writer, const T& value, const Parent& parent)
    {
        Parser<R, W, ReflType, ProcessorsType>::write(
            writer, Reflector<T>::from(value), parent);
    }
};
}
}
