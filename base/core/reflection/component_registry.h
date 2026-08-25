#pragma once

#include <entt/entity/registry.hpp>
#include <entt/meta/meta.hpp>

#include <string_view>
#include <vector>

namespace component_registry
{
using EmplaceFn = void (*)(entt::registry&, entt::entity, entt::meta_any&&);
using CollectFn = entt::meta_any (*)(const entt::registry&, entt::entity);
using HasFn     = bool (*)(const entt::registry&, entt::entity);

struct Binding
{
    std::string_view type_name;
    EmplaceFn        emplace;
    CollectFn        collect;
    HasFn            has;
};

void register_bindings();

[[nodiscard]] const std::vector<Binding>& bindings();

void emplace(entt::registry& registry, entt::entity entity, std::string_view type_name,
             entt::meta_any&& component);

void post_process(entt::registry& registry, entt::entity entity);
}
