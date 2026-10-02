#include "render/scene/GpuScene.h"

#include "asset/Asset.h"
#include "ecs/Render.h"
#include "render/resource/RenderResourceManager.h"

void GpuScene::load(Scene::Desc& scene, RenderResourceManager& resources)
{
    reset();

    uint32_t nextSelectionId = 1;
    for (Scene::Desc::Object& object : scene.objects)
    {
        if (object.components.contains("Render"))
        {
            const auto& render = *object.components.at("Render").try_cast<ecs::Render>();
            items.push_back({&resources.mesh(render.meshId), &object, nextSelectionId++});
        }
        if (object.components.contains("Light"))
        {
            if (lights.empty())
            {
                markers.init(resources);
            }
            lights.push_back({&object, nextSelectionId++});
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

Scene::Desc::Object* GpuScene::findObject(uint32_t selectionId) const
{
    for (const SceneRenderItem& item : items)
    {
        if (item.selectionId == selectionId)
        {
            return item.object;
        }
    }
    for (const LightRenderItem& item : lights)
    {
        if (item.selectionId == selectionId)
        {
            return item.object;
        }
    }
    return nullptr;
}
