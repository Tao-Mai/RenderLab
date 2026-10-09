#pragma once

#include "scene/component/LightComponent.h"

#include <cstdint>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class GpuMesh;
class RenderResourceManager;
struct SceneAsset;
struct TransformComponent;

class LightMarkers
{
public:
    struct DrawItem
    {
        GpuMesh*  mesh;
        glm::mat4 model;
        glm::vec3 color;
        uint32_t  firstIndex;
        uint32_t  indexCount;
        uint32_t  selectionId = 0;
    };

    void prepare(const SceneAsset& scene, RenderResourceManager& resources);
    void reset() noexcept;

    [[nodiscard]] std::span<const DrawItem> drawItems() const { return draws; }

private:
    std::vector<DrawItem> draws;

    [[nodiscard]] std::vector<DrawItem>
    buildDrawItems(const TransformComponent& transform, const LightComponent& light, uint32_t selectionId) const;

    GpuMesh* sphere = nullptr;
    GpuMesh* cube   = nullptr;
    GpuMesh* arrow  = nullptr;
};
