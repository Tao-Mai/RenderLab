#include "render/FrameData.h"

#include "core/Logger.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/editor/EditorShaderData.h"

#include <array>

void FrameData::init(const VulkanContext& targetVulkan, Falcor::Device& device)
{
    vulkan                = &targetVulkan;
    sceneBuffer           = Falcor::Buffer(
        *vulkan,
        sizeof(SceneUniforms),
        vk::BufferUsageFlagBits::eUniformBuffer,
        Falcor::MemoryType::Upload,
        Falcor::ResourceBindFlags::Constant
    );
    lightUniformBuffer = Falcor::Buffer(
        *vulkan,
        sizeof(LightUniforms),
        vk::BufferUsageFlagBits::eUniformBuffer,
        Falcor::MemoryType::Upload,
        Falcor::ResourceBindFlags::Constant
    );
    shadowUniformBuffer = Falcor::Buffer(
        *vulkan, sizeof(ShadowUniforms), vk::BufferUsageFlagBits::eUniformBuffer,
        Falcor::MemoryType::Upload, Falcor::ResourceBindFlags::Constant
    );
    sceneSet    = device.allocateDescriptorSet(device.sceneDescriptorSetLayout);
    lightSet = device.allocateDescriptorSet(device.lightDescriptorSetLayout);
    shadowSet = device.allocateDescriptorSet(device.shadowDescriptorSetLayout);
    viewSet     = device.allocateDescriptorSet(device.editorViewDescriptorSetLayout);
    writeSceneDescriptorSet();
    writeLightUniformDescriptorSet();
    writeShadowUniformDescriptorSet();
    writeEditorViewDescriptorSet();
}

void FrameData::writeSceneDescriptorSet()
{
    const vk::DescriptorBufferInfo bufferInfo{.buffer = sceneBuffer.handle(), .range = sceneBuffer.getSize()};
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
    const vk::DescriptorBufferInfo bufferInfo{.buffer = sceneBuffer.handle(), .range = sceneBuffer.getSize()};
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
    const vk::DescriptorBufferInfo info{.buffer = lightUniformBuffer.handle(), .range = lightUniformBuffer.getSize()};
    const vk::WriteDescriptorSet   write{
          .dstSet          = *lightSet,
          .dstBinding      = RenderInterface::kLightUniformBinding,
          .descriptorCount = 1,
          .descriptorType  = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo     = &info,
    };
    vulkan->deviceHandle().updateDescriptorSets(write, {});
}

void FrameData::writeLightBufferDescriptor(VulkanResources& resources, VulkanResources::BufferHandle handle)
{
    lightBuffer = handle;
    const auto& buffer = resources.getBuffer(lightBuffer);
    const vk::DescriptorBufferInfo info{.buffer = buffer.handle(), .range = buffer.getSize()};
    const vk::WriteDescriptorSet   write{
          .dstSet          = *lightSet,
          .dstBinding      = RenderInterface::kLightBufferBinding,
          .descriptorCount = 1,
          .descriptorType  = vk::DescriptorType::eStorageBuffer,
          .pBufferInfo     = &info,
    };
    vulkan->deviceHandle().updateDescriptorSets(write, {});
}

void FrameData::writeShadowUniformDescriptorSet()
{
    const vk::DescriptorBufferInfo info{.buffer = shadowUniformBuffer.handle(), .range = shadowUniformBuffer.getSize()};
    const vk::WriteDescriptorSet write{
        .dstSet = *shadowSet,
        .dstBinding = RenderInterface::kShadowUniformBinding,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &info,
    };
    vulkan->deviceHandle().updateDescriptorSets(write, {});
}

void FrameData::writeShadowSampler(vk::Sampler sampler)
{
    if (sampler == shadowComparisonSampler)
        return;

    const vk::DescriptorImageInfo info{.sampler = sampler};
    const vk::WriteDescriptorSet  write{
         .dstSet          = *shadowSet,
         .dstBinding      = RenderInterface::kShadowComparisonSamplerBinding,
         .descriptorCount = 1,
         .descriptorType  = vk::DescriptorType::eSampler,
         .pImageInfo      = &info,
    };
    vulkan->deviceHandle().updateDescriptorSets(write, {});
    shadowComparisonSampler = sampler;
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
                .dstSet          = *lightSet,
                .dstBinding      = kImageBindings[index],
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &infos[index],
            },
            vk::WriteDescriptorSet{
                .dstSet          = *lightSet,
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
                .dstSet          = *lightSet,
                .dstBinding      = kImageBindings[index],
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &infos[index],
            },
            vk::WriteDescriptorSet{
                .dstSet          = *lightSet,
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
    lightSet = nullptr;
    shadowSet = nullptr;
    viewSet     = nullptr;
    shadowComparisonSampler = nullptr;
    lightBuffer = {};
    sceneBuffer.reset();
    lightUniformBuffer.reset();
    shadowUniformBuffer.reset();
    vulkan = nullptr;
}

void FrameData::uploadSceneUniforms(const SceneUniforms& view)
{
    DCHECK(vulkan);
    sceneBuffer.setBlob(&view, 0, sizeof(view));
}

void FrameData::uploadLightUniforms(const LightUniforms& lighting)
{
    DCHECK(vulkan);
    lightUniformBuffer.setBlob(&lighting, 0, sizeof(lighting));
}

void FrameData::uploadShadowUniforms(const ShadowUniforms& shadows)
{
    DCHECK(vulkan);
    shadowUniformBuffer.setBlob(&shadows, 0, sizeof(shadows));
}
