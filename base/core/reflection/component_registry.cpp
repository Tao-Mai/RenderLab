#include "base/core/reflection/component_registry.h"

#include "base/component/dynamic_tag.h"
#include "base/component/light.h"
#include "base/component/mesh_renderer.h"
#include "base/component/mesh_renderer_desc.h"
#include "base/component/mesh_source.h"
#include "base/component/tag_name.h"
#include "base/component/transform.h"
#include "base/gfx/mesh_loader.h"
#include "base/gfx/texture_loader.h"

#include <glog/logging.h>

#include <utility>
#include <vector>

namespace
{
std::vector<component_registry::Binding>& binding_storage()
{
    static std::vector<component_registry::Binding> bindings;
    return bindings;
}

template<typename T>
void emplace_copy(entt::registry& registry, entt::entity entity, entt::meta_any&& any)
{
    registry.emplace<T>(entity, any.cast<T>());
}

template<typename T>
entt::meta_any collect_copy(const entt::registry& registry, entt::entity entity)
{
    return entt::meta_any{registry.get<T>(entity)};
}

template<typename T>
bool has_component(const entt::registry& registry, entt::entity entity)
{
    return registry.all_of<T>(entity);
}

void emplace_mesh_renderer(entt::registry& registry, entt::entity entity, entt::meta_any&& any)
{
    const auto desc = any.cast<MeshRendererDesc>();

    MeshRenderer renderer;
    renderer.mesh              = LoadMesh(desc.mesh);
    renderer.material.albedo   = desc.albedo;
    renderer.material.specular = desc.specular;
    renderer.material.shininess = desc.shininess;
    if (!desc.albedo_texture.empty())
        renderer.material.albedo_tex = LoadTexture(desc.albedo_texture);

    const bool dynamic = registry.all_of<DynamicTag>(entity) && registry.get<DynamicTag>(entity).value;
    renderer.upload(dynamic);
    registry.emplace<MeshRenderer>(entity, std::move(renderer));
    registry.emplace_or_replace<MeshSource>(entity, MeshSource{desc.mesh});
}

entt::meta_any collect_mesh_renderer(const entt::registry& registry, entt::entity entity)
{
    if (!registry.all_of<MeshRenderer>(entity))
        return {};

    const auto& runtime = registry.get<MeshRenderer>(entity);
    MeshRendererDesc desc;
    if (registry.all_of<MeshSource>(entity))
        desc.mesh = registry.get<MeshSource>(entity).value;
    desc.albedo    = runtime.material.albedo;
    desc.specular  = runtime.material.specular;
    desc.shininess = runtime.material.shininess;
    // mesh 路径无法从 runtime 反推 asset id，保存时依赖 EntityDesc 里已有的 MeshRenderer 组件
    return entt::meta_any{desc};
}

void bind(std::string_view type_name, component_registry::EmplaceFn emplace,
          component_registry::CollectFn collect, component_registry::HasFn has)
{
    binding_storage().push_back({type_name, emplace, collect, has});
}
}  // namespace

void component_registry::register_bindings()
{
    binding_storage().clear();

    bind("TagName", emplace_copy<TagName>, collect_copy<TagName>, has_component<TagName>);
    bind("Transform", emplace_copy<Transform>, collect_copy<Transform>, has_component<Transform>);
    bind("Light", emplace_copy<Light>, collect_copy<Light>, has_component<Light>);
    bind("DynamicTag", emplace_copy<DynamicTag>, collect_copy<DynamicTag>, has_component<DynamicTag>);
    bind("MeshRenderer", emplace_mesh_renderer, collect_mesh_renderer, has_component<MeshRenderer>);
}

const std::vector<component_registry::Binding>& component_registry::bindings()
{
    return binding_storage();
}

void component_registry::emplace(entt::registry& registry, entt::entity entity,
                                 const std::string_view type_name, entt::meta_any&& component)
{
    for (const Binding& binding : binding_storage())
    {
        if (binding.type_name != type_name)
            continue;
        binding.emplace(registry, entity, std::move(component));
        return;
    }
    LOG(WARNING) << "No component binding for type: " << type_name;
}

void component_registry::post_process(entt::registry& registry, entt::entity entity)
{
    if (registry.all_of<Light, Transform>(entity))
        registry.get<Light>(entity).position = registry.get<Transform>(entity).position;
}
