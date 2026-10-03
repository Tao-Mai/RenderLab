#pragma once

#include "asset/Asset.h"
#include "render/pass/LightMarkers.h"
#include "render/resource/GpuMesh.h"

#include <cstdint>
#include <vector>

class RenderResourceManager;

struct SceneRenderItem
{
    GpuMesh*              mesh        = nullptr;
    AActor*                actor       = nullptr;
    uint32_t              selectionId = 0;
};

struct LightRenderItem
{
    AActor*                actor       = nullptr;
    uint32_t              selectionId = 0;
};

class GpuScene
{
public:
    void load(RenderResourceManager& resources);
    void reset() noexcept;

    [[nodiscard]] const std::vector<SceneRenderItem>& renderItems() const;
    [[nodiscard]] const std::vector<LightRenderItem>& lightRenderItems() const;
    [[nodiscard]] const LightMarkers&                 lightMarkers() const;

private:
    std::vector<SceneRenderItem> items;
    std::vector<LightRenderItem> lights;
    LightMarkers markers;
};
