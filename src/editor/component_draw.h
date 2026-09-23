#pragma once

#include <string_view>

#include <entt/meta/meta.hpp>

[[nodiscard]] bool drawComponent(std::string_view typeName, entt::meta_any& component);
