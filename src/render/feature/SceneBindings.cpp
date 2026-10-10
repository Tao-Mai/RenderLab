#include "render/feature/SceneBindings.h"

#include "asset/Asset.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
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

ShadowPass::Output SceneBindings::setup(RenderGraph& targetGraph, ShadowPass::Output shadow)
{
    graph     = &targetGraph;
    vulkan    = &graph->vulkan();
    resources = renderContext().resources;

    RenderGraph::ImageHandle fallbackShadowMap;
    const auto               resolveShadowMap = [&](RenderGraph::ImageHandle image)
    {
        if (image.id != RenderGraph::kInvalidHandle)
            return image;

        if (fallbackShadowMap.id == RenderGraph::kInvalidHandle)
        {
            // Keep statically used descriptors valid; disabled lights never sample this image.
            constexpr uint32_t kCubeFaceCount = 6;
            fallbackShadowMap                 = graph->createImage({
                                .flags       = vk::ImageCreateFlagBits::eCubeCompatible,
                                .format      = vk::Format::eD32Sfloat,
                                .extent      = {1, 1, 1},
                                .arrayLayers = kCubeFaceCount,
            });
        }
        return fallbackShadowMap;
    };
    pointShadowMap    = resolveShadowMap(shadow.pointLightShadowMap);
    rectAreaShadowMap = resolveShadowMap(shadow.rectAreaLightShadowMap);
    cascadeShadowMap  = resolveShadowMap(shadow.cascadeShadowMap);

    const auto& brdf = context().config->rendererConfig().brdfLut;
    CHECK(!brdf.textureID.empty() && !brdf.samplerID.empty());
    const auto& brdfAsset = context().assetManager->get<TextureAsset>(brdf.textureID);
    CHECK(
        brdfAsset.layout == ImageLayout::Image2D && brdfAsset.colorSpace == ColorSpace::Linear &&
            brdfAsset.format != ImageFormat::R8,
        "BRDF LUT must be a linear 2D texture with at least two channels"
    );
    brdfLutTexture = resources->texture(brdf.textureID);
    brdfLutSampler = resources->sampler(brdf.samplerID);
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

    auto& graph = targetGraph;
    graph.use(
        pointShadowMap,
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

    return {pointShadowMap, rectAreaShadowMap, cascadeShadowMap};
}

void SceneBindings::prepare(RenderGraph& graph) const
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
                .dstSet          = graph.frameData(frameIndex).sceneSetHandle(),
                .dstBinding      = RenderInterface::kPointShadowImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &shadowImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = graph.frameData(frameIndex).sceneSetHandle(),
                .dstBinding      = RenderInterface::kRectAreaShadowImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &rectAreaShadowImage
            },
            vk::WriteDescriptorSet{
                .dstSet          = graph.frameData(frameIndex).sceneSetHandle(),
                .dstBinding      = RenderInterface::kDirectionalShadowImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &directionalShadowImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = graph.frameData(frameIndex).sceneSetHandle(),
                .dstBinding      = RenderInterface::kShadowComparisonSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &shadowSamplerInfo
            },
            vk::WriteDescriptorSet{
                .dstSet          = graph.frameData(frameIndex).sceneSetHandle(),
                .dstBinding      = RenderInterface::kLinearSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &linearSamplerInfo,
            },
        };
        vulkan->deviceHandle().updateDescriptorSets(writes, {});
    }
}

void SceneBindings::bindEnvironmentTextures()
{
    TextureBinding irradianceBinding{BuiltinAssets::Texture::whiteCube, BuiltinAssets::Sampler::linearClamp};
    TextureBinding prefilteredBinding = irradianceBinding;
    const auto&    environmentMap     = context().config->rendererConfig().environmentMap;
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
    const vk::DescriptorImageInfo lutSampler{.sampler = brdfLutSampler};
    for (uint32_t index = 0; index < maxFramesInFlight; ++index)
    {
        FrameData&       frame  = graph->frameData(index);
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
        };
        vulkan->deviceHandle().updateDescriptorSets(writes, {});
    }
}

void SceneBindings::updateFrame(FrameData& frame, const ViewUniforms& view, const RenderData& scene) const
{
    const auto&         feature = context().config->rendererConfig().feature;
    const auto&         shadow  = feature.shadow;
    const LightUniforms lighting{
        .lightCount         = static_cast<uint32_t>(scene.lights.size()),
        .ambientCoefficient = feature.ambientCoefficient,
        .iblParameters      = {prefilteredMaxLod, 0.0f, 0.0f, 0.0f},
        .shadowParameters   = {shadow.bias, shadow.sampleRadius, shadow.pixelRotation ? 1.0f : 0.0f, 0.0f},
    };
    frame.update(view, lighting, scene.lights, shadow);
}
