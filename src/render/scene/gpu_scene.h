#pragma once

#include "render/pass/light_markers.h"
#include "render/resource/mesh.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class AssetManager;
class FrameContext;
class Scene;
class ScenePass;
class VulkanContext;
struct Light;
struct SceneObject;

struct SceneRenderItem
{
    Mesh*        mesh        = nullptr;
    SceneObject* object      = nullptr;
    uint32_t     selectionId = 0;
};

struct LightRenderItem
{
    Light*   light       = nullptr;
    uint32_t selectionId = 0;
};

class GpuScene
{
public:
    void load(
        Scene&         scene,
        AssetManager&  assets,
        VulkanContext& vulkan,
        FrameContext&  frame,
        ScenePass&     scenePass);
    void reset() noexcept;

    [[nodiscard]] const std::vector<SceneRenderItem>& renderItems() const;
    [[nodiscard]] const std::vector<LightRenderItem>& lightRenderItems() const;
    [[nodiscard]] const LightMarkers&                 lightMarkers() const;
    [[nodiscard]] Light*                              primaryLight() const;

    [[nodiscard]] SceneObject* findObject(uint32_t selectionId) const;
    [[nodiscard]] Light*       findLight(uint32_t selectionId) const;

private:
    std::unordered_map<std::string, std::unique_ptr<Mesh>> meshAssets;
    std::vector<SceneRenderItem>                           items;
    std::vector<LightRenderItem>                           lights;
    LightMarkers                                           markers;
    Light*                                                 primary = nullptr;
};
