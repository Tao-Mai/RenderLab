#include "base/core/scene.h"

#include "base/asset/asset_manager.h"
#include "base/asset/reflection/component_registry.h"
#include "base/asset/scene_asset.h"
#include "base/component/camera.h"
#include "base/component/dynamic_tag.h"
#include "base/component/light.h"
#include "base/component/mesh_renderer.h"
#include "base/component/mesh_renderer_desc.h"
#include "base/component/tag_name.h"
#include "base/component/transform.h"
#include "base/io/app_config.h"

#include <glog/logging.h>

namespace
{
void apply_scene_camera(Scene& scene, const SceneCameraConfig& cam)
{
    scene.camera().look_at(cam.position, cam.target);
    scene.camera().fov = cam.fov;
}

bool is_editor_camera_entity(const entt::registry& registry, entt::entity entity)
{
    if (!registry.all_of<TagName>(entity))
        return false;
    return registry.get<TagName>(entity).value == "MainCamera";
}
}  // namespace

Scene::Scene()
{
    ensure_camera_entity();
}

void Scene::clear()
{
    registry_.clear();
    ensure_camera_entity();
}

entt::entity Scene::ensure_camera_entity()
{
    for (auto entity : registry_.view<Camera>())
        return entity;

    const entt::entity entity = registry_.create();
    registry_.emplace<TagName>(entity, TagName{"MainCamera"});
    registry_.emplace<Camera>(entity);
    registry_.emplace<Transform>(entity);
    return entity;
}

Camera& Scene::camera()
{
    const entt::entity entity = ensure_camera_entity();
    return registry_.get<Camera>(entity);
}

const Camera& Scene::camera() const
{
    for (auto entity : registry_.view<Camera>())
        return registry_.get<Camera>(entity);
    static const Camera fallback;
    return fallback;
}

int Scene::mesh_count() const
{
    return static_cast<int>(registry_.view<MeshRenderer>().size());
}

void Scene::instantiate(const SceneAsset& asset, AssetManager& assets,
                        const SceneCameraConfig& app_camera)
{
    clear();
    source_scene_id_ = asset.id;
    clear_color_     = asset.clear_color;
    ambient_         = asset.ambient;

    apply_scene_camera(*this, app_camera);
    if (asset.camera)
        apply_scene_camera(*this, *asset.camera);

    for (const EntityDesc& desc : asset.entities)
    {
        const entt::entity entity = registry_.create();

        std::vector<std::pair<std::string, entt::meta_any>> mesh_components;

        for (const auto& [type_name, component] : desc.components)
        {
            if (type_name == "MeshRenderer")
            {
                mesh_components.emplace_back(type_name, component);
                continue;
            }

            entt::meta_any copy = component;
            component_registry::emplace(registry_, entity, type_name, std::move(copy));
        }

        for (auto& [type_name, component] : mesh_components)
        {
            if (component.type().info() == entt::type_id<MeshRendererDesc>())
            {
                MeshRendererDesc mesh_desc = component.cast<MeshRendererDesc>();
                mesh_desc.mesh             = assets.resolve_mesh_source(mesh_desc.mesh);
                component                  = entt::meta_any{mesh_desc};
            }
            component_registry::emplace(registry_, entity, type_name, std::move(component));
        }

        if (!registry_.all_of<TagName>(entity))
            registry_.emplace<TagName>(entity, TagName{desc.name});

        component_registry::post_process(registry_, entity);
    }

    sync_camera_projection();
    LOG(INFO) << "Instantiated scene '" << asset.name << "' meshes=" << mesh_count()
              << " entities=" << asset.entities.size();
}

bool Scene::sync_to_asset(SceneAsset& asset) const
{
    asset.clear_color = clear_color_;
    asset.ambient     = ambient_;

    SceneCameraConfig cam;
    cam.position = camera().position;
    cam.target   = camera().position + camera().front;
    cam.fov      = camera().fov;
    asset.camera = cam;

    asset.entities.clear();
    for (auto entity : registry_.view<TagName>())
    {
        if (!registry_.valid(entity) || is_editor_camera_entity(registry_, entity))
            continue;

        EntityDesc desc;
        desc.name = scene_ops::tag_name(registry_, entity);

        for (const component_registry::Binding& binding : component_registry::bindings())
        {
            if (!binding.has(registry_, entity))
                continue;
            entt::meta_any component = binding.collect(registry_, entity);
            if (!component)
                continue;
            desc.components.emplace(std::string(binding.type_name), std::move(component));
        }

        asset.entities.push_back(std::move(desc));
    }

    return true;
}
