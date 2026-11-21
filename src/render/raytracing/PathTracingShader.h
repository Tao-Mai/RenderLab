#pragma once

#include "render/PreparedScene.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/RenderContext.h"
#include "render/device/VulkanResources.h"
#include "render/raytracing/PathTracingParameters.h"

#include <array>
#include <vulkan/vulkan.hpp>

inline constexpr std::array kPathTracingMissEntries{"missMain", "visibilityMissMain"};
inline constexpr std::array kPathTracingHitEntries{"closestHitMain", "lightClosestHitMain"};
inline constexpr auto       kPathTracingColorFeatures =
    vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;

inline constexpr auto                       kRayTracingStages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
inline constexpr VulkanResources::ImageAccess kRayTracingImageWrite{
    kRayTracingStages, vk::AccessFlagBits2::eShaderStorageWrite, vk::ImageLayout::eGeneral
};

[[nodiscard]] inline PathTracingParameters makePathTracingParameters(
    const Falcor::RenderContext& frame, uint32_t maxBounces, uint32_t sampleIndex
)
{
    const auto& viewport = frame.view->viewport;
    return {
        .viewport = {viewport.x, viewport.y, viewport.width, viewport.height},
        .lightRanges = frame.scene->lightRanges,
        .maxBounces = maxBounces,
        .sampleIndex = sampleIndex,
        .iblEnabled = frame.config->feature.ibl && frame.scene->environmentMap ? 1u : 0u,
    };
}
