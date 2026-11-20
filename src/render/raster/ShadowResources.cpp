#include "render/raster/ShadowResources.h"

#include "asset/BuiltinAssets.h"
#include "render/FrameData.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderData.h"

#include <algorithm>
#include <array>

ShadowResources createUnshadowedResources(RenderGraph& graph)
{
    constexpr uint32_t kCubeFaceCount = 6;
    const auto         image          = graph.createImage({
                         .flags       = vk::ImageCreateFlagBits::eCubeCompatible,
                         .format      = vk::Format::eD32Sfloat,
                         .extent      = {1, 1, 1},
                         .arrayLayers = kCubeFaceCount,
    });
    const auto         buffer         = graph.createBuffer({
                        .size = std::max(
            {sizeof(PointShadowData), sizeof(SpotShadowData), sizeof(RectAreaShadowData), sizeof(DirectionalShadowData)}
        ),
    });
    return {image, image, image, image, buffer, buffer, buffer, buffer};
}

void useRasterLighting(RenderGraph& graph, RenderGraph::BufferHandle lights, const ShadowResources& shadows)
{
    constexpr RenderGraph::MemoryAccess kStorageRead{
        .stages = vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderStorageRead,
    };
    graph.use(lights, kStorageRead);
    for (const auto buffer :
         {shadows.pointShadowBuffer,
          shadows.spotShadowBuffer,
          shadows.rectAreaShadowBuffer,
          shadows.directionalShadowBuffer})
        graph.use(buffer, kStorageRead);

    constexpr RenderGraph::ImageAccess kSampledRead{
        .stages = vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderSampledRead,
        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    for (const auto image :
         {shadows.pointLightShadowMap,
          shadows.spotLightShadowMap,
          shadows.rectAreaLightShadowMap,
          shadows.cascadeShadowMap})
        graph.use(image, kSampledRead);
}

void bindShadowResources(
    RenderGraph& graph, const ShadowResources& shadows, std::span<FrameData> frames, RenderResourceManager& resources
)
{
    const auto view = [&](RenderGraph::ImageHandle image, vk::ImageViewType type)
    {
        return graph.getImage(image).getOrCreateImageView({
            .viewType         = type,
            .format           = graph.imageInfo(image).format,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .levelCount = 1,
                .layerCount = graph.imageInfo(image).arrayLayers,
            },
        });
    };
    const auto point       = view(shadows.pointLightShadowMap, vk::ImageViewType::eCubeArray);
    const auto spot        = view(shadows.spotLightShadowMap, vk::ImageViewType::e2DArray);
    const auto rectArea    = view(shadows.rectAreaLightShadowMap, vk::ImageViewType::e2DArray);
    const auto directional = view(shadows.cascadeShadowMap, vk::ImageViewType::e2DArray);
    const auto bufferInfo  = [&](RenderGraph::BufferHandle handle)
    {
        const auto& buffer = graph.getBuffer(handle);
        return vk::DescriptorBufferInfo{.buffer = buffer.handle(), .range = buffer.size()};
    };
    const std::array buffers{
        bufferInfo(shadows.pointShadowBuffer),
        bufferInfo(shadows.spotShadowBuffer),
        bufferInfo(shadows.rectAreaShadowBuffer),
        bufferInfo(shadows.directionalShadowBuffer),
    };

    const auto comparisonSampler = resources.sampler(BuiltinAssets::Sampler::kShadowLinear);
    const auto linearSampler     = resources.sampler(BuiltinAssets::Sampler::linearClamp);
    for (auto& frame : frames)
    {
        frame.writeShadowBufferDescriptors(buffers);
        frame.writeShadowDescriptors(point, spot, rectArea, directional, comparisonSampler, linearSampler);
    }
}
