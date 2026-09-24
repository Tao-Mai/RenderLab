#pragma once

#include "render/pass/LightMarkers.h"
#include "render/resource/Mesh.h"

#include <cstdint>
#include <vector>

class RenderResourceManager;
struct SceneDesc;
struct SceneObjectDesc;

struct SceneRenderItem
{
    Mesh*            mesh        = nullptr;
    SceneObjectDesc* object      = nullptr;
    uint32_t         selectionId = 0;
};

struct LightRenderItem
{
    SceneObjectDesc* object      = nullptr;
    uint32_t         selectionId = 0;
};

class GpuScene
{
public:
    void load(SceneDesc& scene, RenderResourceManager& resources);
    void reset() noexcept;

    [[nodiscard]] const std::vector<SceneRenderItem>& renderItems() const;
    [[nodiscard]] const std::vector<LightRenderItem>& lightRenderItems() const;
    [[nodiscard]] const LightMarkers&                 lightMarkers() const;
    [[nodiscard]] SceneObjectDesc*                    primaryLightObject() const;

    [[nodiscard]] SceneObjectDesc* findObject(uint32_t selectionId) const;

private:
    std::vector<SceneRenderItem> items;
    std::vector<LightRenderItem> lights;
    LightMarkers markers;
    SceneObjectDesc* primaryLight = nullptr;
};
