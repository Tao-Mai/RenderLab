#include "render/scene/gpu_scene.h"

#include "asset/asset_manager.h"
#include "render/device/frame_context.h"
#include "render/device/vulkan_context.h"
#include "render/pass/scene_pass.h"
#include "scene/light.h"
#include "scene/scene.h"

#include <utility>

void GpuScene::load(
    Scene&         scene,
    AssetManager&  assets,
    VulkanContext& vulkan,
    FrameContext&  frame,
    ScenePass&     scenePass)
{
    reset();

    if (!scene.lights.empty())
    {
        primary = &scene.lights.front();
        markers.initialize(frame.uploadContext(vulkan), assets);
    }

    uint32_t nextSelectionId = 1;
    for (SceneObject& object : scene.objects)
    {
        auto meshIt = meshAssets.find(object.mesh);
        if (meshIt == meshAssets.end())
        {
            const MeshData& data = assets.loadMesh(object.mesh);
            auto            mesh = std::make_unique<Mesh>(frame.uploadContext(vulkan), data);
            scenePass.createMaterialGpus(*mesh);
            meshIt = meshAssets.emplace(object.mesh, std::move(mesh)).first;
        }
        items.push_back({meshIt->second.get(), &object, nextSelectionId++});
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
    meshAssets.clear();
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

SceneObject* GpuScene::findObject(uint32_t selectionId) const
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
