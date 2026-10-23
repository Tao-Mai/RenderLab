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

#include <cstring>
#include <span>

inline void bindTestLightBuffer(RenderGraph& graph, const PassSetup& setup, RenderGraph::BufferHandle lights)
{
    for (auto& frame : setup.frameData)
        frame.writeLightBufferDescriptor(graph.getBuffer(lights));
}

inline void uploadTestLights(RenderGraph& graph, RenderGraph::BufferHandle buffer, std::span<const LightData> lights)
{
    if (lights.empty())
        return;

    const auto bytes  = lights.size_bytes();
    const auto upload = graph.allocateUpload(buffer, bytes);
    std::memcpy(upload.data.data(), lights.data(), bytes);
}

inline float writeTestEnvironment(const PassSetup& setup, const std::optional<EnvironmentMapAsset::ID>& id)
{
    auto&          resources = *renderContext().resources;
    TextureBinding irradiance{BuiltinAssets::Texture::kBlackCube, BuiltinAssets::Sampler::linearClamp};
    auto           prefiltered = irradiance;
    auto           radiance    = irradiance;
    if (id)
    {
        const auto& environment = context().assetManager->get<EnvironmentMapAsset>(*id);
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
    const auto irradianceInfo  = image(irradiance);
    const auto prefilteredInfo = image(prefiltered);
    const auto radianceInfo    = image(radiance);
    for (auto& frame : setup.frameData)
        frame.writeEnvironmentDescriptors(irradianceInfo, prefilteredInfo, radianceInfo);
    return static_cast<float>(resources.texture(prefiltered.textureID)->imageInfo().mipLevels - 1);
}

// Manual-graph checks own their FrameData and therefore write its shared bindings explicitly.
inline vk::raii::Sampler
writeTestLighting(RenderGraph& graph, const PassSetup& setup, ShadowPass::Output shadow, const RenderData& scene)
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
    const float         prefilteredMaxLod = writeTestEnvironment(setup, scene.environmentMap);
    const auto&         shadowConfig      = feature.shadow;
    const LightUniforms lighting{
        .lightRanges                   = scene.lightRanges,
        .ambientCoefficient            = feature.ambientCoefficient,
        .cascadeCount                  = shadowConfig.cascade.count,
        .directionalShadowSampleRadius = shadowConfig.cascade.sampleRadius,
        .iblParameters                 = {prefilteredMaxLod, 0, 0, 0},
        .shadowParameters              = {
            shadowConfig.bias,
            shadowConfig.sampleRadius,
            shadowConfig.pixelRotation ? 1.0f : 0.0f,
            shadowConfig.spotSampleRadius
        },
    };
    const auto linearSampler = resources.sampler(BuiltinAssets::Sampler::linearClamp);
    const auto bufferInfo    = [&](RenderGraph::BufferHandle handle)
    {
        const auto& buffer = graph.getBuffer(handle);
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
        frame.writeShadowBufferDescriptors(shadowBuffers);
        frame.writeShadowDescriptors(
            view(shadow.pointLightShadowMap, vk::ImageViewType::eCubeArray),
            view(shadow.spotLightShadowMap, vk::ImageViewType::e2DArray),
            view(shadow.rectAreaLightShadowMap, vk::ImageViewType::e2DArray),
            view(shadow.cascadeShadowMap, vk::ImageViewType::e2DArray),
            *sampler,
            linearSampler
        );
        frame.writeLutDescriptors(
            resources.texture(BuiltinAssets::Texture::kBrdfLut)->imageView(),
            resources.texture(BuiltinAssets::Texture::kSheenLut)->imageView(),
            linearSampler
        );
        frame.uploadLightUniforms(lighting);
    }
    return sampler;
}
