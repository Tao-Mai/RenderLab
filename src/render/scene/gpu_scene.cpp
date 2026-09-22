#include "render/scene/gpu_scene.h"

#include "asset/asset_desc.h"
#include "render/resource/render_resource_manager.h"
#include "scene/light.h"

void GpuScene::load(SceneDesc& scene, RenderResourceManager& resources)
{
    reset();

    if (!scene.lights.empty())
    {
        primary = &scene.lights.front();
        markers.init(resources);
    }

    uint32_t nextSelectionId = 1;
    for (SceneObjectDesc& object : scene.objects)
    {
        items.push_back({&resources.mesh(object.meshId), &object, nextSelectionId++});
    }

    for (Light& light : scene.lights)
    {
        lights.push_back({&light, nextSelectionId++});
    }
}

void GpuScene::reset() noexcept
{
    items.clear();
    lights.clear();
    markers.reset();
    primary = nullptr;
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

Light* GpuScene::primaryLight() const
{
    return primary;
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
    return nullptr;
}

Light* GpuScene::findLight(uint32_t selectionId) const
{
    for (const LightRenderItem& item : lights)
    {
        if (item.selectionId == selectionId)
        {
            return item.light;
        }
    }
    return nullptr;
}
