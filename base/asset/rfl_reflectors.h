#pragma once

#include "base/asset/asset_id.h"
#include "base/core/reflection/meta_json.h"

#include <entt/meta/meta.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>

#include <array>
#include <map>
#include <string>

namespace rfl_detail
{
[[nodiscard]] inline nlohmann::json generic_to_json(const rfl::Generic& value)
{
    return nlohmann::json::parse(rfl::json::write(value));
}

[[nodiscard]] inline rfl::Generic json_to_generic(const nlohmann::json& value)
{
    return rfl::json::read<rfl::Generic>(value.dump()).value();
}
}  // namespace rfl_detail

namespace rfl
{
template<>
struct Reflector<glm::vec3>
{
    using ReflType = std::array<float, 3>;

    static glm::vec3 to(const ReflType& v) noexcept { return {v[0], v[1], v[2]}; }

    static ReflType from(const glm::vec3& v) { return {v.x, v.y, v.z}; }
};

template<>
struct Reflector<glm::quat>
{
    // 与场景资产约定一致：欧拉角（度）
    using ReflType = std::array<float, 3>;

    static glm::quat to(const ReflType& v) noexcept
    {
        return glm::quat(glm::radians(glm::vec3{v[0], v[1], v[2]}));
    }

    static ReflType from(const glm::quat& q)
    {
        const glm::vec3 euler = glm::degrees(glm::eulerAngles(q));
        return {euler.x, euler.y, euler.z};
    }
};

template<>
struct Reflector<AssetId>
{
    using ReflType = std::string;

    static AssetId to(const ReflType& text) noexcept
    {
        return parse_asset_id(text).value_or(kInvalidAssetId);
    }

    static ReflType from(const AssetId& id) { return format_asset_id(id); }
};

// 组件表：key = 组件类型名，value 走 meta_json
template<>
struct Reflector<std::map<std::string, entt::meta_any>>
{
    using ReflType = std::map<std::string, Generic>;

    static std::map<std::string, entt::meta_any> to(const ReflType& values) noexcept
    {
        std::map<std::string, entt::meta_any> out;
        for (const auto& [type_name, generic] : values)
        {
            entt::meta_any any =
                meta_json::from_json(std::string_view{type_name}, rfl_detail::generic_to_json(generic));
            if (any)
                out.emplace(type_name, std::move(any));
        }
        return out;
    }

    static ReflType from(const std::map<std::string, entt::meta_any>& values)
    {
        ReflType out;
        for (const auto& [type_name, any] : values)
        {
            nlohmann::json json;
            meta_json::to_json(json, any);
            out.emplace(type_name, rfl_detail::json_to_generic(json));
        }
        return out;
    }
};
}  // namespace rfl
