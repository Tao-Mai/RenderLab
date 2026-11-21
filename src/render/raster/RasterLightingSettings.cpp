#include "render/raster/RasterLightingSettings.h"

#include "core/Logger.h"
#include "render/RenderConfig.h"
#include "render/device/VulkanContext.h"

void RasterLightingSettings::validateColorFormat(const VulkanContext& vulkan) const
{
    if (!hdr)
        return;

    constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eColorAttachment |
        vk::FormatFeatureFlagBits::eColorAttachmentBlend | vk::FormatFeatureFlagBits::eSampledImage |
        vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    const auto features = vulkan.physicalDeviceHandle().getFormatProperties(kHdrColorFormat).optimalTilingFeatures;
    CHECK(
        (features & kRequiredFeatures) == kRequiredFeatures,
        "GPU does not support an RGBA16F HDR color attachment with blending and sampling"
    );
}
