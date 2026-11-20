#include "render/raytracing/RealtimePathTracing.h"

#include "core/Context.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/device/GpuProfiler.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/raytracing/AccelerationStructure.h"
#include "render/raytracing/RealtimeRayTracingBindings.h"
#include "render/resources/GpuShader.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderData.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace
{
constexpr uint32_t kMissShaderCount = 2;
constexpr uint32_t kHitShaderCount  = 2;

vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

struct alignas(16) PathTracingParameters
{
    glm::vec4   viewport;
    LightRanges lightRanges;
    uint32_t    maxBounces;
    uint32_t    sampleIndex;
    uint32_t    iblEnabled;
};
static_assert(sizeof(PathTracingParameters) == 48);
static_assert(offsetof(PathTracingParameters, lightRanges) == 16);
static_assert(offsetof(PathTracingParameters, maxBounces) == 32);
static_assert(offsetof(PathTracingParameters, sampleIndex) == 36);
static_assert(offsetof(PathTracingParameters, iblEnabled) == 40);
}

RealtimePathTracing::Output RealtimePathTracing::compile(
    RenderGraph& graph,
    const PassSetup& setup,
    RealtimeScene::Buffers scene,
    RenderGraph::BufferHandle lights,
    const Program& program
)
{
    CHECK(setup.vulkan.supportsRayTracing(), "selected GPU does not support the ray tracing route");
    const auto& device = setup.vulkan.physicalDeviceHandle();
    const auto features = device.getFormatProperties(kHdrColorFormat).optimalTilingFeatures;
    constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eStorageImage |
        vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    CHECK(
        (features & kRequiredFeatures) == kRequiredFeatures,
        "GPU does not support RGBA16F ray tracing output with storage writes and linear sampling"
    );

    geometry = scene;
    for (const auto buffer : {geometry.objects, geometry.triangles, lights})
        graph.use(
            buffer,
            {
                .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .access = vk::AccessFlagBits2::eShaderStorageRead,
            }
        );
    renderContext().materialTable->use(graph, vk::PipelineStageFlagBits2::eRayTracingShaderKHR);

    extent = setup.renderExtent;
    svgfEnabled = setup.config.feature.svgf;
    sampleIndex = 0;
    output = {};
    const auto createOutput = [&](vk::Format format)
    {
        CHECK(device.getFormatProperties(format).optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage);
        auto image = graph.write(graph.createImage({.format = format, .extent = {extent.width, extent.height, 1}}));
        graph.use(
            image,
            {
                .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .access = vk::AccessFlagBits2::eShaderStorageWrite,
                .layout = vk::ImageLayout::eGeneral,
            }
        );
        return image;
    };
    if (svgfEnabled)
    {
        output.direct   = createOutput(kHdrColorFormat);
        output.indirect = createOutput(kHdrColorFormat);
        output.emission = createOutput(kHdrColorFormat);
        output.albedo   = createOutput(kHdrColorFormat);
    }
    else
        output.rayImage = createOutput(kHdrColorFormat);
    output.depth = createOutput(vk::Format::eR32Sfloat);
    output.worldNormal = createOutput(vk::Format::eR16G16B16A16Sfloat);

    auto& variant = pipelines[svgfEnabled];
    if (!*variant.pipeline)
        createPipeline(setup, variant, program);
    for (auto& frame : frames)
    {
        frame.set = nullptr;
        frame.set = renderContext().device->allocateDescriptorSet(variant.descriptorSetLayout);
        frame.bufferRevisions = {};
        frame.tlasRevision = 0;
    }
    return output;
}

void RealtimePathTracing::createPipeline(const PassSetup& setup, Pipeline& variant, const Program& program)
{
    const auto& vulkan  = setup.vulkan;
    const auto& device  = vulkan.deviceHandle();
    const auto  properties =
        vulkan.physicalDeviceHandle()
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    const auto& limits = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    CHECK(
        kRayPipelineRecursionDepth <= limits.maxRayRecursionDepth,
        "ray tracing requests recursion depth {}, GPU supports {}",
        kRayPipelineRecursionDepth,
        limits.maxRayRecursionDepth
    );

    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings = {
            vk::DescriptorSetLayoutBinding{
                .binding         = RealtimeRayTracingBinding::kTlas,
                .descriptorType  = vk::DescriptorType::eAccelerationStructureKHR,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = RealtimeRayTracingBinding::kObjects,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eClosestHitKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = RealtimeRayTracingBinding::kTriangles,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = RealtimeRayTracingBinding::kColor,
                .descriptorType  = vk::DescriptorType::eStorageImage,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = RealtimeRayTracingBinding::kDepth,
                .descriptorType  = vk::DescriptorType::eStorageImage,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = RealtimeRayTracingBinding::kNormal,
                .descriptorType  = vk::DescriptorType::eStorageImage,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = RealtimeRayTracingBinding::kIndirect,
                .descriptorType  = vk::DescriptorType::eStorageImage,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = RealtimeRayTracingBinding::kEmission,
                .descriptorType  = vk::DescriptorType::eStorageImage,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = RealtimeRayTracingBinding::kAlbedo,
                .descriptorType  = vk::DescriptorType::eStorageImage,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR
            },
        };
        bindings.insert(bindings.end(), program.extraBindings.begin(), program.extraBindings.end());
        if (!svgfEnabled)
        {
            std::erase_if(
                bindings,
                [](const vk::DescriptorSetLayoutBinding& binding)
                {
                    return binding.binding >= RealtimeRayTracingBinding::kIndirect &&
                        binding.binding <= RealtimeRayTracingBinding::kAlbedo;
                }
            );
        }
        auto& renderDevice          = *renderContext().device;
        variant.descriptorSetLayout = renderDevice.createDescriptorSetLayout(bindings);

        const vk::PushConstantRange parameterRange{
            .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR,
            .size       = sizeof(PathTracingParameters),
        };
        const std::array descriptorSetLayouts{
            &renderDevice.sceneDescriptorSetLayout,
            &renderDevice.lightDescriptorSetLayout,
            &renderDevice.materialDescriptorSetLayout,
            &variant.descriptorSetLayout
        };
        variant.layout = renderDevice.getOrCreatePipelineLayout(descriptorSetLayouts, std::array{parameterRange});
    }

    GpuShader shader(device, program.shaderPaths[svgfEnabled]);
    constexpr std::array kSharedEntries{
        "missMain", "visibilityMissMain", "closestHitMain", "lightClosestHitMain",
    };
    std::vector<const char*> entries(program.raygenEntries.begin(), program.raygenEntries.end());
    entries.insert(entries.end(), kSharedEntries.begin(), kSharedEntries.end());
    const auto raygenCount = static_cast<uint32_t>(program.raygenEntries.size());
    std::vector<vk::PipelineShaderStageCreateInfo>      stages(entries.size());
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> groups(entries.size());
    for (uint32_t index = 0; index < entries.size(); ++index)
    {
        const bool hit = index >= raygenCount + kMissShaderCount;
        stages[index]  = {
             .stage  = hit                      ? vk::ShaderStageFlagBits::eClosestHitKHR
                  : index < raygenCount ? vk::ShaderStageFlagBits::eRaygenKHR
                                                : vk::ShaderStageFlagBits::eMissKHR,
             .module = shader.handle(),
             .pName  = entries[index],
        };
        groups[index] = {
            .type =
                hit ? vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup : vk::RayTracingShaderGroupTypeKHR::eGeneral,
            .generalShader      = hit ? VK_SHADER_UNUSED_KHR : index,
            .closestHitShader   = hit ? index : VK_SHADER_UNUSED_KHR,
            .anyHitShader       = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR,
        };
    }
    const vk::RayTracingPipelineCreateInfoKHR info{
        .stageCount                   = static_cast<uint32_t>(stages.size()),
        .pStages                      = stages.data(),
        .groupCount                   = static_cast<uint32_t>(groups.size()),
        .pGroups                      = groups.data(),
        .maxPipelineRayRecursionDepth = kRayPipelineRecursionDepth,
        .layout                       = variant.layout,
    };
    VkPipeline rawPipeline = VK_NULL_HANDLE;
    vkCheck(
        static_cast<vk::Result>(device.getDispatcher()->vkCreateRayTracingPipelinesKHR(
            static_cast<VkDevice>(*device),
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            1,
            reinterpret_cast<const VkRayTracingPipelineCreateInfoKHR*>(&info),
            nullptr,
            &rawPipeline
        ))
    );
    variant.pipeline = vk::raii::Pipeline(device, rawPipeline);

    const vk::DeviceSize stride       = alignUp(limits.shaderGroupHandleSize, limits.shaderGroupHandleAlignment);
    const vk::DeviceSize raygenStride = alignUp(stride, limits.shaderGroupBaseAlignment);
    const vk::DeviceSize missOffset   = raygenStride * raygenCount;
    const vk::DeviceSize hitOffset = alignUp(missOffset + stride * kMissShaderCount, limits.shaderGroupBaseAlignment);
    CHECK(stride <= limits.maxShaderGroupStride, "shader binding table stride exceeds the device limit");
    variant.bindingTable = Buffer(
        vulkan.physicalDeviceHandle(),
        device,
        hitOffset + stride * kHitShaderCount + limits.shaderGroupBaseAlignment,
        vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );
    const vk::DeviceAddress address    = variant.bindingTable.address(device);
    const vk::DeviceAddress base       = alignUp(address, limits.shaderGroupBaseAlignment);
    const uint32_t          groupCount = static_cast<uint32_t>(groups.size());
    std::vector<uint8_t>    handles(limits.shaderGroupHandleSize * groupCount);
    vkCheck(
        static_cast<vk::Result>(device.getDispatcher()->vkGetRayTracingShaderGroupHandlesKHR(
            static_cast<VkDevice>(*device),
            static_cast<VkPipeline>(*variant.pipeline),
            0,
            groupCount,
            handles.size(),
            handles.data()
        ))
    );
    std::vector<uint8_t>        records(variant.bindingTable.size());
    std::vector<vk::DeviceSize> recordOffsets;
    for (uint32_t index = 0; index < raygenCount; ++index)
        recordOffsets.push_back(raygenStride * index);
    for (uint32_t index = 0; index < kMissShaderCount; ++index)
        recordOffsets.push_back(missOffset + stride * index);
    for (uint32_t index = 0; index < kHitShaderCount; ++index)
        recordOffsets.push_back(hitOffset + stride * index);
    for (uint32_t group = 0; group < groupCount; ++group)
        std::memcpy(
            records.data() + base - address + recordOffsets[group],
            handles.data() + limits.shaderGroupHandleSize * group,
            limits.shaderGroupHandleSize
        );
    variant.bindingTable.upload(records.data(), records.size());
    variant.raygenRegions.resize(raygenCount);
    for (uint32_t index = 0; index < raygenCount; ++index)
    {
        variant.raygenRegions[index] = vk::StridedDeviceAddressRegionKHR{
            .deviceAddress = base + raygenStride * index,
            .stride        = stride,
            .size          = stride
        };
    }
    variant.missRegion = vk::StridedDeviceAddressRegionKHR{
        .deviceAddress = base + missOffset,
        .stride        = stride,
        .size          = stride * kMissShaderCount,
    };
    variant.hitRegion = vk::StridedDeviceAddressRegionKHR{
        .deviceAddress = base + hitOffset,
        .stride        = stride,
        .size          = stride * kHitShaderCount
    };
}

void RealtimePathTracing::updateTlasDescriptor(const PassFrame& execution, Frame& frame)
{
    DCHECK(execution.sceneTlas);
    const auto& device    = execution.setup.vulkan.deviceHandle();
    const auto& structure = *execution.sceneTlas;
    if (frame.tlasRevision != structure.revision())
    {
        const auto                                           tlas = structure.handle();
        const vk::WriteDescriptorSetAccelerationStructureKHR accelerationInfo{
            .accelerationStructureCount = 1,
            .pAccelerationStructures    = &tlas,
        };
        const vk::WriteDescriptorSet write{
            .pNext           = &accelerationInfo,
            .dstSet          = *frame.set,
            .dstBinding      = RealtimeRayTracingBinding::kTlas,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eAccelerationStructureKHR,
        };
        device.updateDescriptorSets(write, {});
        frame.tlasRevision = structure.revision();
    }
}

void RealtimePathTracing::bindResources(RenderGraph& graph, const PassSetup& setup)
{
    const auto imageInfo = [&](RenderGraph::ImageHandle image)
    {
        return vk::DescriptorImageInfo{
            .imageView   = graph.getImage(image).getOrCreateImageView({
                  .viewType         = vk::ImageViewType::e2D,
                  .format           = graph.imageInfo(image).format,
                  .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
            }),
            .imageLayout = vk::ImageLayout::eGeneral,
        };
    };
    const auto outputInfo = imageInfo(svgfEnabled ? output.direct : output.rayImage);
    const auto depthInfo  = imageInfo(output.depth);
    const auto normalInfo = imageInfo(output.worldNormal);
    for (const auto& frame : frames)
    {
        const std::array writes{
            vk::WriteDescriptorSet{
                .dstSet          = *frame.set,
                .dstBinding      = RealtimeRayTracingBinding::kColor,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eStorageImage,
                .pImageInfo      = &outputInfo,
            },
            vk::WriteDescriptorSet{
                .dstSet          = *frame.set,
                .dstBinding      = RealtimeRayTracingBinding::kDepth,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eStorageImage,
                .pImageInfo      = &depthInfo,
            },
            vk::WriteDescriptorSet{
                .dstSet          = *frame.set,
                .dstBinding      = RealtimeRayTracingBinding::kNormal,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eStorageImage,
                .pImageInfo      = &normalInfo,
            },
        };
        setup.vulkan.deviceHandle().updateDescriptorSets(writes, {});
    }
    if (svgfEnabled)
    {
        const auto indirectInfo = imageInfo(output.indirect);
        const auto emissionInfo = imageInfo(output.emission);
        const auto albedoInfo   = imageInfo(output.albedo);
        for (const auto& frame : frames)
        {
            const std::array writes{
                vk::WriteDescriptorSet{
                    .dstSet          = *frame.set,
                    .dstBinding      = RealtimeRayTracingBinding::kIndirect,
                    .descriptorCount = 1,
                    .descriptorType  = vk::DescriptorType::eStorageImage,
                    .pImageInfo      = &indirectInfo,
                },
                vk::WriteDescriptorSet{
                    .dstSet          = *frame.set,
                    .dstBinding      = RealtimeRayTracingBinding::kEmission,
                    .descriptorCount = 1,
                    .descriptorType  = vk::DescriptorType::eStorageImage,
                    .pImageInfo      = &emissionInfo,
                },
                vk::WriteDescriptorSet{
                    .dstSet          = *frame.set,
                    .dstBinding      = RealtimeRayTracingBinding::kAlbedo,
                    .descriptorCount = 1,
                    .descriptorType  = vk::DescriptorType::eStorageImage,
                    .pImageInfo      = &albedoInfo,
                },
            };
            setup.vulkan.deviceHandle().updateDescriptorSets(writes, {});
        }
    }
}

void RealtimePathTracing::prepare(RenderGraph& graph, const PassFrame& execution)
{
    constexpr std::array kBindings{RealtimeRayTracingBinding::kObjects, RealtimeRayTracingBinding::kTriangles};
    const std::array handles{geometry.objects, geometry.triangles};
    auto& frame = frames[execution.frameIndex];
    std::array<vk::DescriptorBufferInfo, kBindings.size()> infos;
    std::array<vk::WriteDescriptorSet, kBindings.size()> writes;
    uint32_t writeCount = 0;
    for (uint32_t index = 0; index < handles.size(); ++index)
    {
        const auto revision = graph.bufferRevision(handles[index]);
        if (frame.bufferRevisions[index] == revision)
            continue;

        const auto& buffer = graph.getBuffer(handles[index]);
        infos[index] = vk::DescriptorBufferInfo{.buffer = buffer.handle(), .range = buffer.size()};
        writes[writeCount++] = {
            .dstSet          = *frame.set,
            .dstBinding      = kBindings[index],
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[index],
        };
        frame.bufferRevisions[index] = revision;
    }
    if (writeCount != 0)
        execution.setup.vulkan.deviceHandle().updateDescriptorSets(std::span{writes.data(), writeCount}, {});
}

void RealtimePathTracing::invalidateDenoiserHistory(RenderGraph& graph) const
{
    if (svgfEnabled)
        graph.invalidateImageHistory(output.direct);
}

void RealtimePathTracing::bind(vk::raii::CommandBuffer& commands, const PassFrame& execution, uint32_t maxBounces)
{
    const auto& variant = pipelines[svgfEnabled];
    auto& frame = frames[execution.frameIndex];
    {
        CpuProfileScope timing{context().profiler, "RT.Binding preparation"};
        GpuProfileScope gpuTiming{execution.gpuProfiler, commands, "RT.Binding preparation"};
        updateTlasDescriptor(execution, frame);
    }
    commands.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *variant.pipeline);
    const std::array sets{
        execution.data.sceneSetHandle(),
        execution.data.lightSetHandle(),
        execution.renderData.materialSetHandle(),
        *frame.set
    };
    commands.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, variant.layout, 0, sets, {});

    const auto& viewport = execution.view.viewport;
    commands.pushConstants<PathTracingParameters>(
        variant.layout,
        vk::ShaderStageFlagBits::eRaygenKHR,
        0,
        PathTracingParameters{
            .viewport    = {viewport.x, viewport.y, viewport.width, viewport.height},
            .lightRanges = execution.renderData.lightRanges,
            .maxBounces  = maxBounces,
            .sampleIndex = sampleIndex++,
            .iblEnabled  = execution.setup.config.feature.ibl && execution.renderData.environmentMap ? 1u : 0u,
        }
    );
}

void RealtimePathTracing::dispatch(vk::raii::CommandBuffer& commands, uint32_t raygenIndex) const
{
    const auto& variant = pipelines[svgfEnabled];
    DCHECK(raygenIndex < variant.raygenRegions.size());
    commands.traceRaysKHR(
        variant.raygenRegions[raygenIndex], variant.missRegion, variant.hitRegion, {},
        extent.width, extent.height, 1
    );
}
