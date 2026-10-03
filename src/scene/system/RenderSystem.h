//
// Created by 24500 on 2026/10/3.
//

#pragma once

#include "render/pass/LightMarkers.h"
#include "render/resource/ShaderData.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class AActor;
class GpuMaterial;
class GpuMesh;
class RenderResourceManager;
struct SceneAsset;

struct RenderObject
{
    GpuMesh* mesh;
    AActor* actor;
    uint32_t selectionId;
};

struct LightObject
{
    AActor* actor;
    uint32_t selectionId;
};

struct DrawItem
{
    GpuMesh* mesh;
    const GpuMaterial* material;
    glm::mat4 model;
    uint32_t firstIndex;
    uint32_t indexCount;
    uint32_t selectionId;
    float distanceSquared;
};

struct RenderData
{
    std::vector<RenderObject> objects;
    std::vector<LightObject> lightObjects;
    std::vector<DrawItem> drawItems;
    std::vector<DrawItem> opaqueDrawItems;
    std::vector<DrawItem> transparentDrawItems;
    std::vector<LightMarkers::DrawItem> lightDrawItems;
    std::vector<LightData> lights;
};

class RenderSystem
{
public:
    void load(const SceneAsset& scene, RenderResourceManager& resources);
    void prepare(const glm::vec3& cameraPosition, RenderResourceManager& resources);
    void reset() noexcept;

    [[nodiscard]] const RenderData& renderData() const { return data; }
    [[nodiscard]] const LightMarkers& lightMarkers() const { return markers; }

private:
    RenderData data;
    LightMarkers markers;
};
