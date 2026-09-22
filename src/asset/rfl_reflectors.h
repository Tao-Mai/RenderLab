#pragma once

#include <array>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <rfl.hpp>

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
}
