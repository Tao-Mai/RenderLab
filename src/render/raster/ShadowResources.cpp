#include "render/raster/ShadowResources.h"

#include "asset/BuiltinAssets.h"
#include "render/FrameData.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderData.h"

void bindShadowResources(
    VulkanResources& cache, const ShadowResources& shadows, std::span<FrameData> frames, RenderResourceManager& resources
)
{
    using namespace RenderInterface;
    constexpr auto kSampled          = vk::DescriptorType::eSampledImage;
    constexpr auto kArray            = vk::ImageViewType::e2DArray;
    const auto comparisonSampler = resources.sampler(BuiltinAssets::Sampler::kShadowLinear);
    const auto     linearSampler     = resources.sampler(BuiltinAssets::Sampler::linearClamp);
    for (auto& frame : frames)
    {
        const auto set = frame.shadowSetHandle();
        cache.writeDescriptor(
            set,
            kPointShadowImageBinding,
            shadows.pointLightShadowMap,
            kSampled,
            vk::ImageViewType::eCubeArray
        );
        cache.writeDescriptor(set, kSpotShadowImageBinding, shadows.spotLightShadowMap, kSampled, kArray);
        cache.writeDescriptor(set, kRectAreaShadowImageBinding, shadows.rectAreaLightShadowMap, kSampled, kArray);
        cache.writeDescriptor(set, kDirectionalShadowImageBinding, shadows.cascadeShadowMap, kSampled, kArray);
        cache.writeDescriptor(set, kPointShadowBufferBinding, shadows.pointShadowBuffer);
        cache.writeDescriptor(set, kSpotShadowBufferBinding, shadows.spotShadowBuffer);
        cache.writeDescriptor(set, kRectAreaShadowBufferBinding, shadows.rectAreaShadowBuffer);
        cache.writeDescriptor(set, kDirectionalShadowBufferBinding, shadows.directionalShadowBuffer);
        cache.writeDescriptor(set, kShadowLinearSamplerBinding, linearSampler);
        frame.writeShadowSampler(comparisonSampler);
    }
}
