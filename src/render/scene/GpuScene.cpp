#include "render/scene/GpuScene.h"

#include "asset/Asset.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "ecs/SceneManager.h"
#include "ecs/component/LightComponent.h"
#include "ecs/component/RenderComponent.h"
#include "render/resource/RenderResourceManager.h"

void GpuScene::load(RenderResourceManager& resources)
{
    reset();

    CHECK(context().sceneManager != nullptr, "GpuScene requires SceneManager");
    const auto& manager = *context().sceneManager;
    const auto& registry = manager.registry();
    for (const entt::entity entity : manager.entities())
    {
        const uint32_t selectionId = SceneManager::selectionId(entity);
        if (const auto* render = registry.try_get<ecs::RenderComponent>(entity))
        {
            items.push_back({&resources.mesh(render->meshId), entity, selectionId});
        }
        if (registry.all_of<ecs::LightComponent>(entity))
        {
            if (lights.empty())
            {
                markers.init(resources);
            }
            lights.push_back({entity, selectionId});
        }
    }
}

void GpuScene::reset() noexcept
{
    items.clear();
    lights.clear();
    markers.reset();
}

const std::vector<SceneRenderItem>& GpuScene::renderItems() const
{
    return items;
}

const std::vector<LightRenderItem>& GpuScene::lightRenderItems() const
{
    return lights;
}

const LightMarkers& GpuScene::lightMarkers() const
{
    return markers;
}
