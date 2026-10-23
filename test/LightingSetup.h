#pragma once

#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RendererConfig.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/feature/shadow/ShadowPass.h"
#include "render/resource/RenderResourceManager.h"

#include <memory>

// Manual-graph checks own their FrameData and therefore write its shared bindings explicitly.
inline vk::raii::Sampler writeTestLighting(
    RenderGraph&              graph,
    const PassSetup&          setup,
    ShadowPass::Output        shadow,
    const RenderData&         scene,
    RenderGraph::BufferHandle lights
)
{
    auto&       resources = *renderContext().resources;
    const auto& feature   = setup.config.feature;
    const auto  filter =
        feature.shadow.filter == SamplerAsset::Filter::Linear ? vk::Filter::eLinear : vk::Filter::eNearest;
    auto       sampler = vkCheck(setup.vulkan.deviceHandle().createSampler({
              .magFilter     = filter,
              .minFilter     = filter,
              .mipmapMode    = vk::SamplerMipmapMode::eNearest,
              .addressModeU  = vk::SamplerAddressMode::eClampToEdge,
              .addressModeV  = vk::SamplerAddressMode::eClampToEdge,
              .addressModeW  = vk::SamplerAddressMode::eClampToEdge,
              .compareEnable = true,
              .compareOp     = vk::CompareOp::eLessOrEqual,
    }));
    const auto view    = [&](RenderGraph::ImageHandle image, vk::ImageViewType type)
    {
        return graph.getImage(image).getOrCreateImageView({
            .viewType         = type,
            .format           = vk::Format::eD32Sfloat,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eDepth,
                .levelCount = 1,
                .layerCount = graph.imageInfo(image).arrayLayers,
            },
        });
    };
    TextureBinding irradiance{BuiltinAssets::Texture::whiteCube, BuiltinAssets::Sampler::linearClamp};
    auto           prefiltered = irradiance;
    auto           radiance    = irradiance;
    if (scene.environmentMap)
    {
        const auto& environment = context().assetManager->get<EnvironmentMapAsset>(*scene.environmentMap);
        irradiance              = environment.irradiance;
        prefiltered             = environment.prefilteredSpecular;
        radiance                = environment.radiance;
    }
    const auto image = [&](const TextureBinding& binding)
    {
        return vk::DescriptorImageInfo{
            .sampler     = resources.sampler(binding.samplerID),
            .imageView   = resources.texture(binding.textureID)->imageView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
    };
    const auto&         shadowConfig = feature.shadow;
    const LightUniforms lighting{
        .lightRanges                   = scene.lightRanges,
        .ambientCoefficient            = feature.ambientCoefficient,
        .cascadeCount                  = shadowConfig.cascade.count,
        .directionalShadowSampleRadius = shadowConfig.cascade.sampleRadius,
        .iblParameters =
            {static_cast<float>(resources.texture(prefiltered.textureID)->imageInfo().mipLevels - 1), 0, 0, 0},
        .shadowParameters = {
            shadowConfig.bias,
            shadowConfig.sampleRadius,
            shadowConfig.pixelRotation ? 1.0f : 0.0f,
            shadowConfig.spotSampleRadius
        },
    };
    const auto linearSampler = resources.sampler(BuiltinAssets::Sampler::linearClamp);
    if (!scene.lights.empty())
    {
        const auto upload      = graph.allocateUpload(lights, scene.lights.size() * sizeof(LightData));
        auto*      destination = reinterpret_cast<LightData*>(upload.data.data());
        for (auto light : scene.lights)
        {
            light.shadowEnabled = 0;
            light.shadowidx     = ~0u;
            std::construct_at(destination++, light);
        }
    }
    const auto bufferInfo = [&](RenderGraph::BufferHandle handle)
    {
        const auto& buffer = graph.getBuffer(handle, 0);
        return vk::DescriptorBufferInfo{.buffer = buffer.handle(), .range = buffer.size()};
    };
    const std::array shadowBuffers{
        bufferInfo(shadow.pointShadowBuffer),
        bufferInfo(shadow.spotShadowBuffer),
        bufferInfo(shadow.rectAreaShadowBuffer),
        bufferInfo(shadow.directionalShadowBuffer),
    };
    for (auto& frame : setup.frameData)
    {
        frame.writeLightBufferDescriptor(graph.getBuffer(lights, 0), graph.bufferRevision(lights));
        frame.writeShadowBufferDescriptors(shadowBuffers);
        frame.writeShadowDescriptors(
            view(shadow.pointLightShadowMap, vk::ImageViewType::eCubeArray),
            view(shadow.spotLightShadowMap, vk::ImageViewType::e2DArray),
            view(shadow.rectAreaLightShadowMap, vk::ImageViewType::e2DArray),
            view(shadow.cascadeShadowMap, vk::ImageViewType::e2DArray),
            *sampler,
            linearSampler
        );
        frame.writeEnvironmentDescriptors(image(irradiance), image(prefiltered), image(radiance));
        frame.writeLutDescriptors(
            resources.texture(BuiltinAssets::Texture::kBrdfLut)->imageView(),
            resources.texture(BuiltinAssets::Texture::kSheenLut)->imageView(),
            linearSampler
        );
        frame.uploadLightUniforms(lighting);
    }
    return sampler;
}
