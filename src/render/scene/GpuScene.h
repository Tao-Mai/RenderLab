#pragma once

#include "asset/AssetDesc.h"
#include "render/pass/LightMarkers.h"
#include "render/resource/GpuMesh.h"

#include <cstdint>
#include <vector>

class RenderResourceManager;

struct SceneRenderItem
{
    GpuMesh*              mesh        = nullptr;
    Scene::Desc::Object*  object      = nullptr;
    uint32_t              selectionId = 0;
};

struct LightRenderItem
{
    Scene::Desc::Object* object      = nullptr;
    uint32_t             selectionId = 0;
};

class GpuScene
{
public:
    void load(Scene::Desc& scene, RenderResourceManager& resources);
    void reset() noexcept;

    [[nodiscard]] const std::vector<SceneRenderItem>& renderItems() const;
    [[nodiscard]] const std::vector<LightRenderItem>& lightRenderItems() const;
    [[nodiscard]] const LightMarkers&                 lightMarkers() const;
    [[nodiscard]] Scene::Desc::Object* primaryLightObject() const;

    [[nodiscard]] Scene::Desc::Object* findObject(uint32_t selectionId) const;

private:
    std::vector<SceneRenderItem> items;
    std::vector<LightRenderItem> lights;
    LightMarkers markers;
    Scene::Desc::Object* primaryLight = nullptr;
};
