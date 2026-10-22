#include "render/raytracing/RayTracingPass.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Vertex.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/device/GpuProfiler.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/feature/RenderGraph.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuShader.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr uint32_t kMissShaderCount = 2;
constexpr uint32_t kHitShaderCount  = 2;
vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

RayTracingPass::Output RayTracingPass::setup(RenderGraph& graph, const PassSetup& setup, TlasBuildPass::Output scene)
{
    CHECK(setup.vulkan.supportsRayTracing(), "selected GPU does not support the ray tracing route");
    const auto&    device            = setup.vulkan.physicalDeviceHandle();
    const auto     features          = device.getFormatProperties(kHdrColorFormat).optimalTilingFeatures;
    constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eStorageImage |
        vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    CHECK(
        (features & kRequiredFeatures) == kRequiredFeatures,
        "GPU does not support RGBA16F ray tracing output with storage writes and linear sampling"
    );

    sceneTlas = scene.tlas;
    for (const auto structure : {scene.blas, scene.tlas})
        graph.use(
            structure,
            {
                .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .access = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            }
        );

    const auto extent = setup.swapchain.extent();
    rayImage          = graph.createImage({.format = kHdrColorFormat, .extent = {extent.width, extent.height, 1}});
    rayImage          = graph.write(rayImage);
    graph.use(
        rayImage,
        {
            .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .access = vk::AccessFlagBits2::eShaderStorageWrite,
            .layout = vk::ImageLayout::eGeneral,
        }
    );
    const auto createGBuffer = [&](vk::Format format)
    {
        CHECK(device.getFormatProperties(format).optimalTilingFeatures & vk::FormatFeatureFlagBits::eStorageImage);
        auto image = graph.write(graph.createImage({.format = format, .extent = {extent.width, extent.height, 1}}));
        graph.use(image, {
            .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .access = vk::AccessFlagBits2::eShaderStorageWrite,
            .layout = vk::ImageLayout::eGeneral,
        });
        return image;
    };
    depth = createGBuffer(vk::Format::eR32Sfloat);
    worldNormal = createGBuffer(vk::Format::eR16G16B16A16Sfloat);
    sampleIndex = 0;
    const auto maxRecursionDepth = context().config->rendererConfig().feature.rayTracing.maxRecursionDepth;
    if (!*pipeline || pipelineRecursionDepth != maxRecursionDepth)
        createPipeline(setup, maxRecursionDepth);
    return {rayImage, depth, worldNormal};
}

void RayTracingPass::createPipeline(const PassSetup& setup, uint32_t maxRecursionDepth)
{
    const auto& vulkan = setup.vulkan;
    const auto& device = vulkan.deviceHandle();
    const auto  properties =
        vulkan.physicalDeviceHandle()
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    const auto& limits = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    CHECK(maxRecursionDepth >= kMinRayRecursionDepth);
    CHECK(
        maxRecursionDepth <= limits.maxRayRecursionDepth,
        "ray tracing requests recursion depth {}, GPU supports {}",
        maxRecursionDepth,
        limits.maxRayRecursionDepth
    );

    if (!pipelineLayout)
    {
        const std::array bindings = {
            vk::DescriptorSetLayoutBinding{
                .binding         = 0,
                .descriptorType  = vk::DescriptorType::eAccelerationStructureKHR,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = 1,
                .descriptorType  = vk::DescriptorType::eStorageImage,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = 2,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eClosestHitKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = 5,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eClosestHitKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding = 7, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding = 8, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR
            },
        };
        auto& renderDevice = *renderContext().device;
        descriptorSetLayout = renderDevice.createDescriptorSetLayout(bindings);
        for (auto& frame : frames)
            frame.set = renderDevice.allocateDescriptorSet(descriptorSetLayout);

        const vk::PushConstantRange parameterRange{
            .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR,
            .size       = sizeof(RayTracingParameters),
        };
        const std::array descriptorSetLayouts{
            &renderDevice.sceneDescriptorSetLayout,
            &renderDevice.lightingDescriptorSetLayout,
            &renderDevice.materialDescriptorSetLayout,
            &descriptorSetLayout
        };
        pipelineLayout = renderDevice.getOrCreatePipelineLayout(descriptorSetLayouts, std::array{parameterRange});
    }

    GpuShader        shader(device, "shaders/raytracing.spv");
    const std::array stages = {
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eRaygenKHR,
            .module = shader.handle(),
            .pName  = "raygenMain"
        },
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eMissKHR,
            .module = shader.handle(),
            .pName  = "missMain"
        },
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eMissKHR,
            .module = shader.handle(),
            .pName  = "visibilityMissMain"
        },
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eClosestHitKHR,
            .module = shader.handle(),
            .pName  = "closestHitMain"
        },
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eClosestHitKHR,
            .module = shader.handle(),
            .pName  = "lightClosestHitMain"
        },
    };
    const std::array groups = {
        vk::RayTracingShaderGroupCreateInfoKHR{
            .type               = vk::RayTracingShaderGroupTypeKHR::eGeneral,
            .generalShader      = 0,
            .closestHitShader   = VK_SHADER_UNUSED_KHR,
            .anyHitShader       = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
        vk::RayTracingShaderGroupCreateInfoKHR{
            .type               = vk::RayTracingShaderGroupTypeKHR::eGeneral,
            .generalShader      = 1,
            .closestHitShader   = VK_SHADER_UNUSED_KHR,
            .anyHitShader       = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
        vk::RayTracingShaderGroupCreateInfoKHR{
            .type               = vk::RayTracingShaderGroupTypeKHR::eGeneral,
            .generalShader      = 2,
            .closestHitShader   = VK_SHADER_UNUSED_KHR,
            .anyHitShader       = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
        vk::RayTracingShaderGroupCreateInfoKHR{
            .type               = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
            .generalShader      = VK_SHADER_UNUSED_KHR,
            .closestHitShader   = 3,
            .anyHitShader       = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
        vk::RayTracingShaderGroupCreateInfoKHR{
            .type               = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup,
            .generalShader      = VK_SHADER_UNUSED_KHR,
            .closestHitShader   = 4,
            .anyHitShader       = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR
        },
    };
    const vk::RayTracingPipelineCreateInfoKHR info{
        .stageCount                   = static_cast<uint32_t>(stages.size()),
        .pStages                      = stages.data(),
        .groupCount                   = static_cast<uint32_t>(groups.size()),
        .pGroups                      = groups.data(),
        .maxPipelineRayRecursionDepth = maxRecursionDepth,
        .layout                       = pipelineLayout,
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
    pipeline               = vk::raii::Pipeline(device, rawPipeline);
    pipelineRecursionDepth = maxRecursionDepth;

    const vk::DeviceSize stride     = alignUp(limits.shaderGroupHandleSize, limits.shaderGroupHandleAlignment);
    const vk::DeviceSize missOffset = alignUp(stride, limits.shaderGroupBaseAlignment);
    const vk::DeviceSize hitOffset  = alignUp(missOffset + stride * kMissShaderCount, limits.shaderGroupBaseAlignment);
    CHECK(stride <= limits.maxShaderGroupStride, "shader binding table stride exceeds the device limit");
    bindingTable.reset();
    bindingTable = Buffer(
        vulkan.physicalDeviceHandle(),
        device,
        hitOffset + stride * kHitShaderCount + limits.shaderGroupBaseAlignment,
        vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );
    const vk::DeviceAddress address     = bindingTable.address(device);
    const vk::DeviceAddress base        = alignUp(address, limits.shaderGroupBaseAlignment);
    const uint32_t          kGroupCount = static_cast<uint32_t>(groups.size());
    std::vector<uint8_t>    handles(limits.shaderGroupHandleSize * kGroupCount);
    vkCheck(
        static_cast<vk::Result>(device.getDispatcher()->vkGetRayTracingShaderGroupHandlesKHR(
            static_cast<VkDevice>(*device),
            static_cast<VkPipeline>(*pipeline),
            0,
            kGroupCount,
            handles.size(),
            handles.data()
        ))
    );
    std::vector<uint8_t> records(bindingTable.size());
    // Group order: raygen, primary miss, visibility miss, surface hit, light hit.
    const std::array<vk::DeviceSize, 5>
        recordOffsets{0, missOffset, missOffset + stride, hitOffset, hitOffset + stride};
    for (uint32_t group = 0; group < kGroupCount; ++group)
        std::memcpy(
            records.data() + base - address + recordOffsets[group],
            handles.data() + limits.shaderGroupHandleSize * group,
            limits.shaderGroupHandleSize
        );
    bindingTable.upload(records.data(), records.size());
    raygenRegion = vk::StridedDeviceAddressRegionKHR{.deviceAddress = base, .stride = stride, .size = stride};
    missRegion   = vk::StridedDeviceAddressRegionKHR{
          .deviceAddress = base + missOffset,
          .stride        = stride,
          .size          = stride * kMissShaderCount,
    };
    hitRegion = vk::StridedDeviceAddressRegionKHR{
        .deviceAddress = base + hitOffset,
        .stride        = stride,
        .size          = stride * kHitShaderCount
    };
}

void RayTracingPass::prepareLights(const PassFrame& execution, Frame& frame) const
{
    const auto&          vulkan = execution.setup.vulkan;
    const auto&          lights = execution.renderData.lights;
    const vk::DeviceSize bytes  = std::max<size_t>(lights.size(), 1) * sizeof(LightData);
    CHECK(bytes <= vulkan.properties().limits.maxStorageBufferRange, "ray tracing lights exceed maxStorageBufferRange");

    if (frame.lightData.size() < bytes)
    {
        frame.lightData.reset();
        frame.lightData = Buffer(
            vulkan.physicalDeviceHandle(),
            vulkan.deviceHandle(),
            bytes,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
    }
    if (!lights.empty())
        frame.lightData.upload(lights.data(), lights.size() * sizeof(LightData));
}

void RayTracingPass::prepareBindings(RenderGraph& graph, const PassFrame& execution, Frame& frame)
{
    const auto& vulkan = execution.setup.vulkan;
    const auto& device = vulkan.deviceHandle();
    const auto& items  = execution.renderData.drawItems;
    static_assert(sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24);

    std::vector<RayTracingHitData> hits;
    hits.reserve(items.size());
    for (const auto& item : items)
        hits.push_back({
            item.mesh->vertices().address(device),
            item.mesh->indices().address(device),
            item.firstIndex,
            item.materialIndex,
        });

    const vk::DeviceSize bytes = std::max<size_t>(hits.size(), 1) * sizeof(RayTracingHitData);
    CHECK(
        bytes <= vulkan.properties().limits.maxStorageBufferRange,
        "ray tracing hit data exceeds maxStorageBufferRange"
    );
    if (frame.hitData.size() < bytes)
    {
        frame.hitData.reset();
        frame.hitData = Buffer(
            vulkan.physicalDeviceHandle(),
            device,
            bytes,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
    }
    if (!hits.empty())
        frame.hitData.upload(hits.data(), hits.size() * sizeof(hits.front()));

    const vk::AccelerationStructureKHR                   tlas = graph.getAccelerationStructure(sceneTlas).handle();
    const vk::WriteDescriptorSetAccelerationStructureKHR accelerationInfo{
        .accelerationStructureCount = 1,
        .pAccelerationStructures    = &tlas
    };
    const vk::DescriptorImageInfo output{
        .imageView   = graph.getImage(rayImage).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = graph.imageInfo(rayImage).format,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eGeneral
    };
    const auto gBufferInfo = [&](RenderGraph::ImageHandle image)
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
    const auto depthInfo = gBufferInfo(depth);
    const auto normalInfo = gBufferInfo(worldNormal);
    const vk::DescriptorBufferInfo hitInfo{.buffer = frame.hitData.handle(), .range = frame.hitData.size()};
    const vk::DescriptorBufferInfo lightInfo{.buffer = frame.lightData.handle(), .range = frame.lightData.size()};
    const std::array writes = {
        vk::WriteDescriptorSet{
            .pNext           = &accelerationInfo,
            .dstSet          = *frame.set,
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eAccelerationStructureKHR
        },
        vk::WriteDescriptorSet{
            .dstSet          = *frame.set,
            .dstBinding      = 1,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageImage,
            .pImageInfo      = &output
        },
        vk::WriteDescriptorSet{
            .dstSet          = *frame.set,
            .dstBinding      = 2,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &hitInfo
        },
        vk::WriteDescriptorSet{
            .dstSet          = *frame.set,
            .dstBinding      = 5,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &lightInfo
        },
        vk::WriteDescriptorSet{
            .dstSet = *frame.set, .dstBinding = 7, .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &depthInfo,
        },
        vk::WriteDescriptorSet{
            .dstSet = *frame.set, .dstBinding = 8, .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &normalInfo,
        },
    };
    device.updateDescriptorSets(writes, {});
}

void RayTracingPass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution)
{
    auto&  commands = commandBuffer;
    Frame& frame    = frames[execution.frameIndex];
    {
        CpuProfileScope timing{context().profiler, "RT.Binding preparation"};
        GpuProfileScope gpuTiming{execution.gpuProfiler, commands, "RT.Binding preparation"};
        prepareLights(execution, frame);
        prepareBindings(graph, execution, frame);
    }
    commands.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
    const std::array sets{
        execution.data.sceneSetHandle(), execution.data.lightingSetHandle(),
        execution.renderData.materialSetHandle(), *frame.set
    };
    commands.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, pipelineLayout, 0, sets, {});
    const auto& viewport = execution.view.viewport;
    commands.pushConstants<RayTracingParameters>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR,
        0,
        RayTracingParameters{
            .viewport              = {viewport.x, viewport.y, viewport.width, viewport.height},
            .lightRanges           = execution.renderData.lightRanges,
            .maxRecursionDepth     = pipelineRecursionDepth,
            .sampleIndex           = sampleIndex++,
        }
    );
    const auto extent = execution.setup.swapchain.extent();
    GpuProfileScope timing{execution.gpuProfiler, commands, "RT.Trace rays"};
    commands.traceRaysKHR(raygenRegion, missRegion, hitRegion, {}, extent.width, extent.height, 1);
}
