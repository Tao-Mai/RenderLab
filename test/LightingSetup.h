#pragma once

#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "render/FrameData.h"
#include "render/PreparedScene.h"
#include "render/RendererConfig.h"
#include "render/device/Device.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanResources.h"
#include "render/resources/RenderResourceManager.h"

#include <cstring>
#include <span>

inline Falcor::ref<Falcor::Buffer> createTestLightBuffer(Falcor::Device& device)
{
    return device.createBuffer(
        kMaxLights * sizeof(LightData), Falcor::ResourceBindFlags::ShaderResource, Falcor::MemoryType::DeviceLocal
    );
}

inline void bindTestLightBuffer(VulkanResources& cache, std::span<FrameData> frames, VulkanResources::BufferHandle lights)
{
    for (auto& frame : frames)
        frame.writeLightBufferDescriptor(cache, lights);
}

inline void
uploadTestLights(VulkanResources& cache, VulkanResources::BufferHandle buffer, std::span<const LightData> lights)
{
    if (lights.empty())
        return;

    const auto bytes  = lights.size_bytes();
    const auto upload = cache.allocateUpload(buffer, bytes);
    std::memcpy(upload.data.data(), lights.data(), bytes);
}

inline float writeTestEnvironment(
    RenderResourceManager& resources, std::span<FrameData> frames, const std::optional<EnvironmentMapAsset::ID>& id
)
{
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
    for (auto& frame : frames)
        frame.writeEnvironmentDescriptors(irradianceInfo, prefilteredInfo, radianceInfo);
    return static_cast<float>(resources.texture(prefiltered.textureID)->imageInfo().mipLevels - 1);
}

// Manual-graph checks own their FrameData and therefore write its shared bindings explicitly.
inline void writeTestLighting(
    RenderResourceManager& resources, std::span<FrameData> frames, const RenderFeature& feature, const PreparedScene& scene
)
{
    const float prefilteredMaxLod = writeTestEnvironment(resources, frames, scene.environmentMap);
    const LightUniforms lighting{
        .lightRanges = scene.lightRanges,
        .ambientCoefficient = feature.ambientCoefficient,
        .prefilteredMaxLod = prefilteredMaxLod,
    };
    const auto linearSampler = resources.sampler(BuiltinAssets::Sampler::linearClamp);
    for (auto& frame : frames)
    {
        frame.writeLutDescriptors(
            resources.texture(BuiltinAssets::Texture::kBrdfLut)->imageView(),
            resources.texture(BuiltinAssets::Texture::kSheenLut)->imageView(),
            linearSampler
        );
        frame.uploadLightUniforms(lighting);
    }
}
