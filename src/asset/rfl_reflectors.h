#pragma once

#include "core/logger.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

#include <entt/core/hashed_string.hpp>
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
[[nodiscard]] inline rfl::Generic metaAnyToGeneric(const entt::meta_any& value)
{
    if (!value)
    {
        return {};
    }

    if (const auto* v = value.try_cast<bool>())
    {
        return rfl::Generic{*v};
    }
    if (const auto* v = value.try_cast<std::int64_t>())
    {
        return rfl::Generic{*v};
    }
    if (const auto* v = value.try_cast<int>())
    {
        return rfl::Generic{static_cast<std::int64_t>(*v)};
    }
    if (const auto* v = value.try_cast<std::uint32_t>())
    {
        return rfl::Generic{static_cast<std::int64_t>(*v)};
    }
    if (const auto* v = value.try_cast<float>())
    {
        return rfl::Generic{static_cast<double>(*v)};
    }
    if (const auto* v = value.try_cast<double>())
    {
        return rfl::Generic{*v};
    }
    if (const auto* v = value.try_cast<std::string>())
    {
        return rfl::Generic{*v};
    }
    if (const auto* v = value.try_cast<glm::vec2>())
    {
        return rfl::to_generic(*v);
    }
    if (const auto* v = value.try_cast<glm::vec3>())
    {
        return rfl::to_generic(*v);
    }
    if (const auto* v = value.try_cast<glm::vec4>())
    {
        return rfl::to_generic(*v);
    }
    if (const auto* v = value.try_cast<glm::quat>())
    {
        return rfl::to_generic(*v);
    }

    const entt::meta_type type = value.type();
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
        const auto parsed = value.to_int();
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{static_cast<int>(*parsed)};
    }
    if (expected == entt::resolve<std::int64_t>())
    {
        const auto parsed = value.to_int64();
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{*parsed};
    }
    if (expected == entt::resolve<std::uint32_t>())
    {
        const auto parsed = value.to_int();
        CHECK(parsed, "{}", parsed.error().what());
        return entt::meta_any{static_cast<std::uint32_t>(*parsed)};
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
