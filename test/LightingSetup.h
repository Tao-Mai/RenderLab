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
#include "render/raster/ShadowResources.h"
#include "render/resources/RenderResourceManager.h"

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
inline void writeTestLighting(
    RenderGraph& graph, const PassSetup& setup, const ShadowResources& shadows, const RenderData& scene
)
{
    auto&       resources = *renderContext().resources;
    const auto& feature   = setup.config.feature;
    bindShadowResources(graph, shadows, setup.frameData, resources);
    const float         prefilteredMaxLod = writeTestEnvironment(setup, scene.environmentMap);
    const LightUniforms lighting{
        .lightRanges = scene.lightRanges,
        .ambientCoefficient = feature.ambientCoefficient,
        .prefilteredMaxLod = prefilteredMaxLod,
    };
    const auto linearSampler = resources.sampler(BuiltinAssets::Sampler::linearClamp);
    for (auto& frame : setup.frameData)
    {
        frame.writeLutDescriptors(
            resources.texture(BuiltinAssets::Texture::kBrdfLut)->imageView(),
            resources.texture(BuiltinAssets::Texture::kSheenLut)->imageView(),
            linearSampler
        );
        frame.uploadLightUniforms(lighting);
        if (!feature.shadows)
            frame.uploadShadowUniforms({.enabled = 0u});
    }
}
