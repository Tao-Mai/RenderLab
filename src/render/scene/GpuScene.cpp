#include "render/scene/GpuScene.h"

#include "asset/Asset.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "scene/SceneManager.h"
#include "scene/component/LightComponent.h"
#include "scene/component/RenderComponent.h"
#include "render/resource/RenderResourceManager.h"

void GpuScene::load(RenderResourceManager& resources)
{
    reset();

    CHECK(context().sceneManager != nullptr, "GpuScene requires SceneManager");
    const auto& manager = *context().sceneManager;
    for (const auto& actor : manager.scene().actors)
    {
        const uint32_t selectionId = actor->selectionId();
        if (const auto* render = actor->getComponent<RenderComponent>())
        {
            items.push_back({&resources.mesh(render->meshId), actor.get(), selectionId});
        }
        if (actor->getComponent<LightComponent>())
        {
            if (lights.empty())
            {
                markers.init(resources);
            }
            lights.push_back({actor.get(), selectionId});
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
