#include "base/core/scene_ops.h"

namespace scene_ops
{
bool has_mesh(const entt::registry& reg, entt::entity entity)
{
    return reg.valid(entity) && reg.all_of<MeshRenderer>(entity);
}

glm::mat4 model(const entt::registry& reg, entt::entity entity)
{
    if (!reg.valid(entity) || !reg.all_of<Transform>(entity))
        return glm::mat4(1.0f);
    return reg.get<Transform>(entity).to_model();
}

void draw(const entt::registry& reg, entt::entity entity)
{
    if (has_mesh(reg, entity))
        reg.get<MeshRenderer>(entity).draw();
}

std::vector<entt::entity> mesh_entities(const entt::registry& reg)
{
    std::vector<entt::entity> result;
    for (auto entity : reg.view<MeshRenderer>())
        result.push_back(entity);
    return result;
}

std::string tag_name(const entt::registry& reg, entt::entity entity)
{
    if (reg.valid(entity) && reg.all_of<TagName>(entity))
        return reg.get<TagName>(entity).value;
    return {};
}
}  // namespace scene_ops
