#pragma once

#include "render/pass/light_markers.h"
#include "render/resource/mesh.h"

#include <cstdint>
#include <vector>

class RenderResourceManager;
struct SceneDesc;
namespace ecs { struct Light; }
struct SceneObjectDesc;

struct SceneRenderItem
{
    Mesh*        mesh        = nullptr;
    SceneObjectDesc* object  = nullptr;
    uint32_t     selectionId = 0;
};

struct LightRenderItem
{
    ecs::Light*   light       = nullptr;
    uint32_t selectionId = 0;
};

class GpuScene
{
public:
    void load(SceneDesc& scene, RenderResourceManager& resources);
    void reset() noexcept;

    [[nodiscard]] const std::vector<SceneRenderItem>& renderItems() const;
    [[nodiscard]] const std::vector<LightRenderItem>& lightRenderItems() const;
    [[nodiscard]] const LightMarkers&                 lightMarkers() const;
    [[nodiscard]] ecs::Light*                              primaryLight() const;

    [[nodiscard]] SceneObjectDesc* findObject(uint32_t selectionId) const;
    [[nodiscard]] ecs::Light*       findLight(uint32_t selectionId) const;

private:
    std::vector<SceneRenderItem> items;
    std::vector<LightRenderItem> lights;
    LightMarkers markers;
    ecs::Light* primary = nullptr;
};
