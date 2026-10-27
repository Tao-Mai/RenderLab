#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "core/math/Bounds.h"
#include "render/DrawItem.h"
#include "render/Visibility.h"
#include "render/resources/ShaderData.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan.hpp>

struct RenderData
{
    vk::DescriptorSet                           materialDescriptorSet;
    std::optional<AssetID<EnvironmentMapAsset>> environmentMap;
    // Borrowed for preparation and command recording; the submission outlives both.
    std::span<const DrawItem> drawItems;
    // Aligned with drawItems; values are indices into the global material SSBO.
    std::vector<uint32_t> materialIndices;
    std::vector<Aabb>     drawBounds;
    // Empty when this path does not perform main-view culling.
    std::vector<uint32_t>  visibilityMasks;
    std::vector<LightData> lights;
    LightRanges            lightRanges;

    [[nodiscard]] vk::DescriptorSet materialSetHandle() const { return materialDescriptorSet; }
    [[nodiscard]] bool              isVisible(uint32_t drawItemIndex) const
    {
        return visibilityMasks.empty() || (visibilityMasks[drawItemIndex] & RenderVisibility::kMainView);
    }
};
