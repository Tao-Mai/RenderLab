#include "render/feature/SceneBindings.h"

#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/EnvironmentMapAsset.h"
#include "asset/SamplerAsset.h"
#include "asset/TextureAsset.h"
#include "asset/TextureBinding.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resource/GpuTexture.h"
#include "render/resource/RenderResourceManager.h"

#include <array>

ShadowPass::Output SceneBindings::setup(RenderGraph& targetGraph, const PassSetup& setup, ShadowPass::Output shadow)
{
    auto& graph = targetGraph;
    vulkan      = &setup.vulkan;
    frameData   = setup.frameData;
    resources   = renderContext().resources;

    RenderGraph::ImageHandle fallbackShadowMap;
    const auto               resolveShadowMap = [&](RenderGraph::ImageHandle image)
    {
        if (image.id != RenderGraph::kInvalidHandle)
            return image;

        if (fallbackShadowMap.id == RenderGraph::kInvalidHandle)
        {
            // Keep statically used descriptors valid; disabled lights never sample this image.
            constexpr uint32_t kCubeFaceCount = 6;
            fallbackShadowMap                 = graph.createImage({
                                .flags       = vk::ImageCreateFlagBits::eCubeCompatible,
                                .format      = vk::Format::eD32Sfloat,
                                .extent      = {1, 1, 1},
                                .arrayLayers = kCubeFaceCount,
            });
        }
        return fallbackShadowMap;
    };
    pointShadowMap    = resolveShadowMap(shadow.pointLightShadowMap);
    spotShadowMap     = resolveShadowMap(shadow.spotLightShadowMap);
    rectAreaShadowMap = resolveShadowMap(shadow.rectAreaLightShadowMap);
    cascadeShadowMap  = resolveShadowMap(shadow.cascadeShadowMap);

    refreshBindings();

    graph.use(
        pointShadowMap,
        {
            .stages = vk::PipelineStageFlagBits2::eFragmentShader,
            .access = vk::AccessFlagBits2::eShaderSampledRead,
            .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }
    );
    graph.use(
        spotShadowMap,
        {
            .stages = vk::PipelineStageFlagBits2::eFragmentShader,
            .access = vk::AccessFlagBits2::eShaderSampledRead,
            .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }
    );
    graph.use(
        rectAreaShadowMap,
        {
            .stages = vk::PipelineStageFlagBits2::eFragmentShader,
            .access = vk::AccessFlagBits2::eShaderSampledRead,
            .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }
    );
    graph.use(
        cascadeShadowMap,
        {
            .stages = vk::PipelineStageFlagBits2::eFragmentShader,
            .access = vk::AccessFlagBits2::eShaderSampledRead,
            .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }
    );

    return {pointShadowMap, spotShadowMap, rectAreaShadowMap, cascadeShadowMap};
}

void SceneBindings::prepare(RenderGraph& graph, const PassSetup& setup) const
{
    const vk::DescriptorImageInfo linearSamplerInfo{
        .sampler = resources->sampler(BuiltinAssets::Sampler::linearClamp),
    };

    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex)
    {
        const vk::DescriptorImageInfo shadowImage{
            .imageView = graph.getImage(pointShadowMap, frameIndex)
                             .getOrCreateImageView({
                                 .viewType = vk::ImageViewType::eCubeArray,
                                 .format   = vk::Format::eD32Sfloat,
                                 .subresourceRange =
                                     {.aspectMask = vk::ImageAspectFlagBits::eDepth,
                                      .levelCount = 1,
                                      .layerCount = graph.imageInfo(pointShadowMap).arrayLayers},
                             }),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        const vk::DescriptorImageInfo spotShadowImage{
            .imageView = graph.getImage(spotShadowMap, frameIndex)
                             .getOrCreateImageView({
                                 .viewType = vk::ImageViewType::e2DArray,
                                 .format   = vk::Format::eD32Sfloat,
                                 .subresourceRange =
                                     {.aspectMask = vk::ImageAspectFlagBits::eDepth,
                                      .levelCount = 1,
                                      .layerCount = graph.imageInfo(spotShadowMap).arrayLayers},
                             }),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        const vk::DescriptorImageInfo rectAreaShadowImage{
            .imageView = graph.getImage(rectAreaShadowMap, frameIndex)
                             .getOrCreateImageView({
                                 .viewType = vk::ImageViewType::e2DArray,
                                 .format   = vk::Format::eD32Sfloat,
                                 .subresourceRange =
                                     {.aspectMask = vk::ImageAspectFlagBits::eDepth,
                                      .levelCount = 1,
                                      .layerCount = graph.imageInfo(rectAreaShadowMap).arrayLayers},
                             }),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        const vk::DescriptorImageInfo shadowSamplerInfo{.sampler = *shadowSampler};
        const vk::DescriptorImageInfo directionalShadowImage{
            .imageView = graph.getImage(cascadeShadowMap, frameIndex)
                             .getOrCreateImageView({
                                 .viewType = vk::ImageViewType::e2DArray,
                                 .format   = vk::Format::eD32Sfloat,
                                 .subresourceRange =
                                     {
                                         .aspectMask = vk::ImageAspectFlagBits::eDepth,
                                         .levelCount = 1,
                                         .layerCount = graph.imageInfo(cascadeShadowMap).arrayLayers,
                                     },
                             }),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        const std::array writes = {
            vk::WriteDescriptorSet{
                .dstSet          = setup.frameData[frameIndex].sceneSetHandle(),
                .dstBinding      = RenderInterface::kPointShadowImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &shadowImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = setup.frameData[frameIndex].sceneSetHandle(),
                .dstBinding      = RenderInterface::kSpotShadowImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &spotShadowImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = setup.frameData[frameIndex].sceneSetHandle(),
                .dstBinding      = RenderInterface::kRectAreaShadowImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &rectAreaShadowImage
            },
            vk::WriteDescriptorSet{
                .dstSet          = setup.frameData[frameIndex].sceneSetHandle(),
                .dstBinding      = RenderInterface::kDirectionalShadowImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &directionalShadowImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = setup.frameData[frameIndex].sceneSetHandle(),
                .dstBinding      = RenderInterface::kShadowComparisonSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &shadowSamplerInfo
            },
            vk::WriteDescriptorSet{
                .dstSet          = setup.frameData[frameIndex].sceneSetHandle(),
                .dstBinding      = RenderInterface::kLinearSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &linearSamplerInfo,
            },
        };
        vulkan->deviceHandle().updateDescriptorSets(writes, {});
    }
}

void SceneBindings::updateEnvironmentMap(const std::optional<EnvironmentMapAsset::ID>& id)
{
    if (environmentMap == id)
        return;

    DCHECK(vulkan);
    // The environment descriptors are shared by all frames in flight.
    vkCheck(vulkan->deviceHandle().waitIdle());
    environmentMap = id;
    bindEnvironmentTextures();
}

void SceneBindings::bindEnvironmentTextures()
{
    TextureBinding irradianceBinding{BuiltinAssets::Texture::whiteCube, BuiltinAssets::Sampler::linearClamp};
    TextureBinding prefilteredBinding = irradianceBinding;
    TextureBinding radianceBinding    = irradianceBinding;
    if (environmentMap)
    {
        const auto& environment = context().assetManager->get<EnvironmentMapAsset>(*environmentMap);
        CHECK(
            !environment.irradiance.textureID.empty() && !environment.irradiance.samplerID.empty() &&
                !environment.prefilteredSpecular.textureID.empty() &&
                !environment.prefilteredSpecular.samplerID.empty(),
            "environment '{}' requires irradiance and prefiltered specular textures",
            environment.id
        );
        radianceBinding          = environment.radiance;
        const auto& radianceDesc = context().assetManager->get<TextureAsset>(radianceBinding.textureID);
        CHECK(
            radianceDesc.layout == ImageLayout::Cubemap && radianceDesc.colorSpace == ColorSpace::Linear,
            "environment '{}' requires a linear radiance cubemap",
            environment.id
        );
        irradianceBinding  = environment.irradiance;
        prefilteredBinding = environment.prefilteredSpecular;

        const TextureAsset& irradianceDesc  = context().assetManager->get<TextureAsset>(irradianceBinding.textureID);
        const TextureAsset& prefilteredDesc = context().assetManager->get<TextureAsset>(prefilteredBinding.textureID);
        CHECK(
            irradianceDesc.layout == ImageLayout::Cubemap && irradianceDesc.colorSpace == ColorSpace::Linear &&
                prefilteredDesc.layout == ImageLayout::Cubemap && prefilteredDesc.colorSpace == ColorSpace::Linear &&
                prefilteredDesc.mipLevels > 0,
            "environment '{}' requires linear irradiance and prefiltered cubemaps",
            environment.id
        );
    }

    radianceTexture            = resources->texture(radianceBinding.textureID);
    irradianceTexture          = resources->texture(irradianceBinding.textureID);
    prefilteredSpecularTexture = resources->texture(prefilteredBinding.textureID);
    prefilteredMaxLod          = static_cast<float>(prefilteredSpecularTexture->imageInfo().mipLevels - 1);
    const vk::DescriptorImageInfo irradianceImage{
        .imageView   = irradianceTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo irradianceSampler{.sampler = resources->sampler(irradianceBinding.samplerID)};
    const vk::DescriptorImageInfo prefilteredImage{
        .imageView   = prefilteredSpecularTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo prefilteredSampler{.sampler = resources->sampler(prefilteredBinding.samplerID)};
    const vk::DescriptorImageInfo lutImage{
        .imageView   = brdfLutTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo lutSampler{.sampler = resources->sampler(BuiltinAssets::Sampler::linearClamp)};
    const vk::DescriptorImageInfo sheenImage{
        .imageView   = sheenLutTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    const vk::DescriptorImageInfo radianceImage{
        .imageView   = radianceTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };
    const vk::DescriptorImageInfo radianceSampler{.sampler = resources->sampler(radianceBinding.samplerID)};
    for (uint32_t index = 0; index < maxFramesInFlight; ++index)
    {
        FrameData&       frame  = frameData[index];
        const std::array writes = {
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kIrradianceImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &irradianceImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kIrradianceSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &irradianceSampler,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kPrefilteredSpecularImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &prefilteredImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kPrefilteredSpecularSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &prefilteredSampler,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kBrdfLutImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &lutImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kBrdfLutSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &lutSampler,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kSheenLutImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &sheenImage
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kSheenLutSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &lutSampler
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kRadianceImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &radianceImage
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kRadianceSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &radianceSampler
            },
        };
        vulkan->deviceHandle().updateDescriptorSets(writes, {});
    }
}

void SceneBindings::updateFrame(FrameData& frame, const ViewUniforms& view, const ShadowFrameData& prepared) const
{
    const auto&   feature = context().config->rendererConfig().feature;
    const auto&   shadow  = feature.shadow;
    LightUniforms lighting{
        .lightRanges                   = prepared.lightRanges,
        .ambientCoefficient            = feature.ambientCoefficient,
        .directionalShadowSampleRadius = shadow.cascade.sampleRadius,
        .iblParameters                 = {prefilteredMaxLod, 0.0f, 0.0f, 0.0f},
        .shadowParameters =
            {shadow.bias, shadow.sampleRadius, shadow.pixelRotation ? 1.0f : 0.0f, shadow.spotSampleRadius},
    };
    lighting.cascadeCount = shadow.cascade.count;
    frame.update(view, lighting, prepared);
}

void SceneBindings::refreshBindings()
{
    brdfLutTexture = resources->texture(BuiltinAssets::Texture::kBrdfLut);
    sheenLutTexture = resources->texture(BuiltinAssets::Texture::kSheenLut);
    bindEnvironmentTextures();

    const vk::Filter filter = context().config->rendererConfig().feature.shadow.filter == SamplerAsset::Filter::Linear
        ? vk::Filter::eLinear
        : vk::Filter::eNearest;
    if (!*shadowSampler || shadowFilter != filter)
    {
        shadowSampler = vkCheck(vulkan->deviceHandle().createSampler(
            {.magFilter     = filter,
             .minFilter     = filter,
             .mipmapMode    = vk::SamplerMipmapMode::eNearest,
             .addressModeU  = vk::SamplerAddressMode::eClampToEdge,
             .addressModeV  = vk::SamplerAddressMode::eClampToEdge,
             .addressModeW  = vk::SamplerAddressMode::eClampToEdge,
             .compareEnable = true,
             .compareOp     = vk::CompareOp::eLessOrEqual,
             .maxLod        = 0.0f}
        ));
        shadowFilter  = filter;
    }
}
