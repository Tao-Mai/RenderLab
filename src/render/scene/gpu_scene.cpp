#include "render/scene/gpu_scene.h"

#include "asset/asset_desc.h"
#include "ecs/light.h"
#include "ecs/render.h"
#include "render/resource/render_resource_manager.h"

void GpuScene::load(SceneDesc& scene, RenderResourceManager& resources)
{
    reset();

    uint32_t nextSelectionId = 1;
    for (SceneObjectDesc& object : scene.objects)
    {
        if (object.components.contains("Render"))
        {
            const auto& render = *object.components.at("Render").try_cast<ecs::Render>();
            items.push_back({&resources.mesh(render.meshId), &object, nextSelectionId++});
        }
        if (object.components.contains("Light"))
        {
            if (primaryLight == nullptr)
            {
                primaryLight = &object;
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
    primaryLight = nullptr;
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

SceneObjectDesc* GpuScene::primaryLightObject() const
{
    return primaryLight;
}

SceneObjectDesc* GpuScene::findObject(uint32_t selectionId) const
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
