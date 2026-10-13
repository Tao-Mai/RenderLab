#pragma once

#include "render/resource/ShaderData.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/mat4x4.hpp>

class GpuMaterial;
class GpuMesh;

struct DrawItem
{
    GpuMesh*                           mesh;
    std::shared_ptr<const GpuMaterial> material;
    glm::mat4                          model;
    uint32_t                           firstIndex;
    uint32_t                           indexCount;
    uint32_t                           selectionId;
    float                              distanceSquared;
    uint32_t                           visibilityMask = 0;
};

struct RenderData
{
    std::vector<DrawItem>  drawItems;
    std::vector<DrawItem>  opaqueDrawItems;
    std::vector<DrawItem>  transparentDrawItems;
    std::vector<LightData> lights;
};
