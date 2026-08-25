#pragma once

#include "base/component/camera.h"
#include "base/component/dynamic_tag.h"
#include "base/component/light.h"
#include "base/component/mesh_renderer.h"
#include "base/component/tag_name.h"
#include "base/component/transform.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace scene_ops
{
[[nodiscard]] bool has_mesh(const entt::registry& reg, entt::entity entity);
[[nodiscard]] glm::mat4 model(const entt::registry& reg, entt::entity entity);
void draw(const entt::registry& reg, entt::entity entity);
[[nodiscard]] std::vector<entt::entity> mesh_entities(const entt::registry& reg);
[[nodiscard]] std::string tag_name(const entt::registry& reg, entt::entity entity);
}  // namespace scene_ops
