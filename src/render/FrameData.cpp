#include "render/FrameData.h"

#include "core/Logger.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/feature/editor/EditorShaderData.h"

#include <array>

void FrameData::init(const VulkanContext& targetVulkan, Device& device)
{
    vulkan                = &targetVulkan;
    const auto hostMemory = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    sceneBuffer           = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        sizeof(SceneUniforms),
        vk::BufferUsageFlagBits::eUniformBuffer,
        hostMemory
    );
    lightUniformBuffer = Buffer(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        sizeof(LightUniforms),
        vk::BufferUsageFlagBits::eUniformBuffer,
        hostMemory
    );
    sceneSet    = device.allocateDescriptorSet(device.sceneDescriptorSetLayout);
    lightingSet = device.allocateDescriptorSet(device.lightingDescriptorSetLayout);
    viewSet     = device.allocateDescriptorSet(device.editorViewDescriptorSetLayout);
    writeSceneDescriptorSet();
    writeLightUniformDescriptorSet();
    writeEditorViewDescriptorSet();
}

void FrameData::writeSceneDescriptorSet()
{
    const vk::DescriptorBufferInfo bufferInfo{.buffer = sceneBuffer.handle(), .range = sceneBuffer.size()};
    const vk::WriteDescriptorSet   write{
          .dstSet          = *sceneSet,
          .dstBinding      = RenderInterface::kSceneUniformBinding,
          .descriptorCount = 1,
          .descriptorType  = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo     = &bufferInfo,
    };
    vulkan->deviceHandle().updateDescriptorSets(write, {});
}

void FrameData::writeEditorViewDescriptorSet()
{
    const vk::DescriptorBufferInfo bufferInfo{.buffer = sceneBuffer.handle(), .range = sceneBuffer.size()};
    const vk::WriteDescriptorSet   write{
          .dstSet          = *viewSet,
          .dstBinding      = EditorInterface::kViewUniformBinding,
          .descriptorCount = 1,
          .descriptorType  = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo     = &bufferInfo,
    };
    vulkan->deviceHandle().updateDescriptorSets(write, {});
}

void FrameData::writeLightUniformDescriptorSet()
{
    const vk::DescriptorBufferInfo info{.buffer = lightUniformBuffer.handle(), .range = lightUniformBuffer.size()};
    const vk::WriteDescriptorSet   write{
          .dstSet          = *lightingSet,
          .dstBinding      = RenderInterface::kLightUniformBinding,
          .descriptorCount = 1,
          .descriptorType  = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo     = &info,
    };
    vulkan->deviceHandle().updateDescriptorSets(write, {});
}

void FrameData::writeLightBufferDescriptor(const Buffer& buffer, uint64_t revision)
{
    const vk::DescriptorBufferInfo info{.buffer = buffer.handle(), .range = buffer.size()};
    const vk::WriteDescriptorSet   write{
          .dstSet          = *lightingSet,
          .dstBinding      = RenderInterface::kLightBufferBinding,
          .descriptorCount = 1,
          .descriptorType  = vk::DescriptorType::eStorageBuffer,
          .pBufferInfo     = &info,
    };
    vulkan->deviceHandle().updateDescriptorSets(write, {});
    boundLightBufferRevision = revision;
}

void FrameData::writeShadowBufferDescriptors(std::span<const vk::DescriptorBufferInfo, 4> buffers)
{
    constexpr std::array kBindings{
        RenderInterface::kPointShadowBufferBinding,
        RenderInterface::kSpotShadowBufferBinding,
        RenderInterface::kRectAreaShadowBufferBinding,
        RenderInterface::kDirectionalShadowBufferBinding,
    };
    std::array<vk::WriteDescriptorSet, kBindings.size()> writes;
    for (size_t index = 0; index < writes.size(); ++index)
        writes[index] = {
            .dstSet          = *lightingSet,
            .dstBinding      = kBindings[index],
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &buffers[index],
        };
    vulkan->deviceHandle().updateDescriptorSets(writes, {});
}

void FrameData::writeShadowDescriptors(
    vk::ImageView pointShadow,
    vk::ImageView spotShadow,
    vk::ImageView rectAreaShadow,
    vk::ImageView directionalShadow,
    vk::Sampler   comparisonSampler,
    vk::Sampler   linearSampler
)
{
    const std::array infos{
        vk::DescriptorImageInfo{.imageView = pointShadow, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::DescriptorImageInfo{.imageView = spotShadow, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::DescriptorImageInfo{.imageView = rectAreaShadow, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::DescriptorImageInfo{.imageView = directionalShadow, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal},
        vk::DescriptorImageInfo{.sampler = comparisonSampler},
        vk::DescriptorImageInfo{.sampler = linearSampler},
    };
    constexpr std::array kBindings{
        RenderInterface::kPointShadowImageBinding,
        RenderInterface::kSpotShadowImageBinding,
        RenderInterface::kRectAreaShadowImageBinding,
        RenderInterface::kDirectionalShadowImageBinding,
        RenderInterface::kShadowComparisonSamplerBinding,
        RenderInterface::kLinearSamplerBinding,
    };
    constexpr size_t                                     kShadowImageCount = 4;
    std::array<vk::WriteDescriptorSet, kBindings.size()> writes;
    for (size_t index = 0; index < writes.size(); ++index)
        writes[index] = {
            .dstSet          = *lightingSet,
            .dstBinding      = kBindings[index],
            .descriptorCount = 1,
            .descriptorType =
                index < kShadowImageCount ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eSampler,
            .pImageInfo = &infos[index],
        };
    vulkan->deviceHandle().updateDescriptorSets(writes, {});
}

void FrameData::writeShadowSampler(vk::Sampler sampler)
{
    const vk::DescriptorImageInfo info{.sampler = sampler};
    const vk::WriteDescriptorSet  write{
         .dstSet          = *lightingSet,
         .dstBinding      = RenderInterface::kShadowComparisonSamplerBinding,
         .descriptorCount = 1,
         .descriptorType  = vk::DescriptorType::eSampler,
         .pImageInfo      = &info,
    };
    vulkan->deviceHandle().updateDescriptorSets(write, {});
}

void FrameData::writeEnvironmentDescriptors(
    const vk::DescriptorImageInfo& irradiance,
    const vk::DescriptorImageInfo& prefilteredSpecular,
    const vk::DescriptorImageInfo& radiance
)
{
    const std::array     infos{irradiance, prefilteredSpecular, radiance};
    constexpr std::array kImageBindings{
        RenderInterface::kIrradianceImageBinding,
        RenderInterface::kPrefilteredSpecularImageBinding,
        RenderInterface::kRadianceImageBinding,
    };
    constexpr std::array kSamplerBindings{
        RenderInterface::kIrradianceSamplerBinding,
        RenderInterface::kPrefilteredSpecularSamplerBinding,
        RenderInterface::kRadianceSamplerBinding,
    };
    for (size_t index = 0; index < infos.size(); ++index)
    {
        const std::array writes{
            vk::WriteDescriptorSet{
                .dstSet          = *lightingSet,
                .dstBinding      = kImageBindings[index],
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &infos[index],
            },
            vk::WriteDescriptorSet{
                .dstSet          = *lightingSet,
                .dstBinding      = kSamplerBindings[index],
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &infos[index],
            },
        };
        vulkan->deviceHandle().updateDescriptorSets(writes, {});
    }
}

void FrameData::writeLutDescriptors(vk::ImageView brdfLut, vk::ImageView sheenLut, vk::Sampler sampler)
{
    const std::array infos{
        vk::DescriptorImageInfo{
            .sampler     = sampler,
            .imageView   = brdfLut,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        },
        vk::DescriptorImageInfo{
            .sampler     = sampler,
            .imageView   = sheenLut,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        },
    };
    constexpr std::array kImageBindings{RenderInterface::kBrdfLutImageBinding, RenderInterface::kSheenLutImageBinding};
    constexpr std::array kSamplerBindings{
        RenderInterface::kBrdfLutSamplerBinding,
        RenderInterface::kSheenLutSamplerBinding
    };
    for (size_t index = 0; index < infos.size(); ++index)
    {
        const std::array writes{
            vk::WriteDescriptorSet{
                .dstSet          = *lightingSet,
                .dstBinding      = kImageBindings[index],
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &infos[index],
            },
            vk::WriteDescriptorSet{
                .dstSet          = *lightingSet,
                .dstBinding      = kSamplerBindings[index],
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &infos[index],
            },
        };
        vulkan->deviceHandle().updateDescriptorSets(writes, {});
    }
}

void FrameData::reset() noexcept
{
    sceneSet    = nullptr;
    lightingSet = nullptr;
    viewSet     = nullptr;
    sceneBuffer.reset();
    lightUniformBuffer.reset();
    boundLightBufferRevision = 0;
    vulkan                   = nullptr;
}

void FrameData::uploadSceneUniforms(const SceneUniforms& view)
{
    DCHECK(vulkan);
    sceneBuffer.upload(&view, sizeof(view));
}

void FrameData::uploadLightUniforms(const LightUniforms& lighting)
{
    DCHECK(vulkan);
    lightUniformBuffer.upload(&lighting, sizeof(lighting));
}
