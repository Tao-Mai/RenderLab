#pragma once

#include "core/math/Bounds.h"
#include "render/DrawItem.h"
#include "render/Visibility.h"
#include "render/raytracing/RayScene.h"
#include "render/resources/SceneLighting.h"
#include "render/resources/SceneShading.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>

struct GpuEnvironmentMap;
struct RenderContext;
class AssetManager;
class RenderResourceManager;
class SceneAccelerationStructures;
enum class SceneInputs : uint32_t;

struct PreparedScene
{
    uint64_t                 sceneGeneration = 0;
    const GpuEnvironmentMap* environmentMap  = nullptr;
    // Borrowed for preparation and command recording; the submission outlives both.
    std::span<const DrawItem> drawItems;
    std::vector<Aabb>         drawBounds;
    // Empty when this path does not perform main-view culling.
    std::vector<uint32_t>        visibilityMasks;
    const SceneShadingData*      shading = nullptr;
    SceneLighting                lighting;
    RayScene                     rays;
    rhi::IAccelerationStructure* tlas = nullptr;

    void init(rhi::IDevice& device, RenderResourceManager& resources);
    void prepare(
        const RenderContext&         frame,
        SceneInputs                  sceneInputs,
        const AssetManager&          assets,
        SceneAccelerationStructures& acceleration
    );

    [[nodiscard]] std::span<const LightData> lights() const { return shading->lights; }
    [[nodiscard]] const LightRanges&         lightRanges() const { return shading->lightRanges; }
    [[nodiscard]] rhi::IBuffer*              globalsBuffer() const { return globals; }

    [[nodiscard]] uint32_t materialIndex(uint32_t drawItemIndex) const
    {
        return shading->draws[drawItemIndex].materialIndex;
    }

    [[nodiscard]] bool isVisible(uint32_t drawItemIndex) const
    {
        return visibilityMasks.empty() || (visibilityMasks[drawItemIndex] & RenderVisibility::kMainView);
    }

private:
    rhi::ComPtr<rhi::IBuffer> globals;
    std::optional<uint64_t>   uploadedLightRevision;
};
