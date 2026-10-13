#include "render/feature/SceneInterface.h"
#include <array>
#include "render/DescriptorManager.h"
#include "render/PipelineManager.h"
#include "render/resource/ShaderData.h"
vk::DescriptorSetLayout SceneInterface::sceneLayout(DescriptorManager& descriptors)
{
    const std::array sceneBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kViewUniformBinding,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kLightUniformBinding,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kLightBufferBinding,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kPointShadowBufferBinding,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kSpotShadowBufferBinding,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kRectAreaShadowBufferBinding,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kDirectionalShadowBufferBinding,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },

        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kPointShadowImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kSpotShadowImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kRectAreaShadowImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kDirectionalShadowImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kShadowComparisonSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kLinearSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },

        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kIrradianceImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kIrradianceSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kPrefilteredSpecularImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kPrefilteredSpecularSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kBrdfLutImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kBrdfLutSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kRadianceImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kRadianceSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
    };
    return descriptors.getOrCreateLayout(sceneBindings);
}
vk::DescriptorSetLayout SceneInterface::materialLayout(DescriptorManager& descriptors)
{
    const std::array materialBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialUniformBinding,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialNormalImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialNormalSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialMetallicImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialMetallicSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialRoughnessImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialRoughnessSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialEmissiveImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialEmissiveSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialOcclusionImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialOcclusionSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialClearcoatImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialClearcoatSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialClearcoatRoughnessImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialClearcoatRoughnessSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialClearcoatNormalImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialClearcoatNormalSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
    };
    return descriptors.getOrCreateLayout(materialBindings);
}
vk::PipelineLayout SceneInterface::meshLayout(DescriptorManager& descriptors, PipelineManager& pipelines)
{
    const std::array sets{sceneLayout(descriptors), materialLayout(descriptors)};
    const std::array ranges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(MeshPushConstants)
    }};
    return pipelines.getOrCreateLayout(sets, ranges);
}
vk::PipelineLayout SceneInterface::shadowLayout(DescriptorManager& descriptors, PipelineManager& pipelines)
{
    const std::array sets{sceneLayout(descriptors)};
    const std::array ranges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(ShadowPushConstants)
    }};
    return pipelines.getOrCreateLayout(sets, ranges);
}
vk::PipelineLayout SceneInterface::skyboxLayout(DescriptorManager& descriptors, PipelineManager& pipelines)
{
    const std::array sets{sceneLayout(descriptors)};
    const std::array ranges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(SkyboxPushConstants)
    }};
    return pipelines.getOrCreateLayout(sets, ranges);
}
