#pragma once

#include "render/feature/editor/EditorFrameInput.h"

#include <cstdint>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class GpuMesh;
class RenderResourceManager;
struct SceneAsset;
struct TransformComponent;
struct LightComponent;

class LightMarkers
{
public:
    void prepare(const SceneAsset& scene, RenderResourceManager& resources);
    void reset() noexcept;

    [[nodiscard]] std::span<const LightMarkerDrawItem> drawItems() const { return draws; }

private:
    std::vector<LightMarkerDrawItem> draws;

    [[nodiscard]] std::vector<LightMarkerDrawItem>
    buildDrawItems(const TransformComponent& transform, const LightComponent& light, uint32_t selectionId) const;

    GpuMesh* sphere = nullptr;
    GpuMesh* cube   = nullptr;
    GpuMesh* arrow  = nullptr;
    GpuMesh* cone   = nullptr;
};
