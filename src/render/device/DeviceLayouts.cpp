#include "render/device/Device.h"

#include "core/Logger.h"
#include "render/feature/editor/EditorShaderData.h"
#include "render/resource/MaterialData.h"
#include "render/resource/ShaderData.h"

#include <array>

void Device::createSharedLayouts()
{
    createSharedDescriptorSetLayouts();
    createSharedPipelineLayouts();
}

void Device::createSharedDescriptorSetLayouts()
{
    constexpr std::array sceneBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kSceneUniformBinding,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eAll
        },
    };
    sceneDescriptorSetLayout = createDescriptorSetLayout(sceneBindings);

    constexpr std::array lightingBindings = {
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
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kSheenLutImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kSheenLutSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
    };
    lightingDescriptorSetLayout = createDescriptorSetLayout(lightingBindings);

    constexpr std::array materialBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialBufferBinding,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eAll,
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialTexturesBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = kMaxMaterialTextures,
            .stageFlags      = vk::ShaderStageFlagBits::eAll,
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kMaterialSamplersBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = kMaxMaterialSamplers,
            .stageFlags      = vk::ShaderStageFlagBits::eAll,
        },
    };
    materialDescriptorSetLayout = createDescriptorSetLayout(materialBindings);

    constexpr std::array editorBindings{vk::DescriptorSetLayoutBinding{
        .binding         = EditorInterface::kViewUniformBinding,
        .descriptorType  = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    }};
    editorViewDescriptorSetLayout = createDescriptorSetLayout(editorBindings);
}

void Device::createSharedPipelineLayouts()
{
    const std::array meshSets{&sceneDescriptorSetLayout, &lightingDescriptorSetLayout, &materialDescriptorSetLayout};
    constexpr std::array meshRanges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(MeshPushConstants)
    }};
    meshPipelineLayout = getOrCreatePipelineLayout(meshSets, meshRanges);

    const std::array     editorSets{&editorViewDescriptorSetLayout};
    constexpr std::array editorRanges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(EditorLightMarkerPushConstants),
    }};
    editorPipelineLayout = getOrCreatePipelineLayout(editorSets, editorRanges);
}
