#pragma once

#include <entt/meta/meta.hpp>
#include <nlohmann/json_fwd.hpp>

namespace meta_json
{
[[nodiscard]] entt::meta_type resolve_type(std::string_view type_name);

[[nodiscard]] nlohmann::json to_json(const entt::meta_any& value);
[[nodiscard]] entt::meta_any from_json(std::string_view type_name, const nlohmann::json& j);

void apply_json(entt::meta_any& value, const nlohmann::json& j);
}
