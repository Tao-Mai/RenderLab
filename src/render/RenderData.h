#pragma once

#include "core/math/Bounds.h"
#include "render/Visibility.h"
#include "scene/system/SceneRenderData.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan.hpp>

class GpuMaterial;

struct RenderData
{
    struct MaterialBinding
    {
        std::shared_ptr<GpuMaterial> material;
        uint32_t                     materialIndex = 0;
    };

    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, MaterialBinding> materials;
    vk::DescriptorSet                                                   materialDescriptorSet;
    std::optional<AssetID<EnvironmentMapAsset>>                         environmentMap;
    std::span<const RenderObject>                                       objects;
    uint32_t                                                            submeshCount = 0;
    std::vector<Aabb>                                                   objectBounds;
    // Empty when this path does not perform main-view culling.
    std::vector<uint32_t>  visibilityMasks;
    std::vector<LightData> lights;
    LightRanges            lightRanges;

    [[nodiscard]] vk::DescriptorSet      materialSetHandle() const { return materialDescriptorSet; }
    [[nodiscard]] const MaterialBinding& material(const RenderObject& object, uint32_t submeshIndex) const
    {
        return materials.find({object.selectionId, object.instanceIndex, submeshIndex})->second;
    }
    [[nodiscard]] bool isVisible(uint32_t objectIndex) const
    {
        return visibilityMasks.empty() || (visibilityMasks[objectIndex] & RenderVisibility::kMainView);
    }
};
