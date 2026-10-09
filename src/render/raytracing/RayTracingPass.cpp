#include "render/raytracing/RayTracingPass.h"

#include "asset/BuiltinAssets.h"
#include "core/Logger.h"
#include "core/Vertex.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/feature/RenderGraph.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuShader.h"
#include "render/resource/RenderResourceManager.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr uint32_t kRayRecursionDepth = 2;
constexpr uint32_t kMissShaderCount   = 2;

vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

void buildBarrier(vk::raii::CommandBuffer& commands, bool trace)
{
    const vk::MemoryBarrier2 barrier{
        .srcStageMask  = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        .srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
        .dstStageMask  = trace ? vk::PipelineStageFlagBits2::eRayTracingShaderKHR
                               : vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        .dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
    };
    commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
}
}  // namespace

RayTracingPass::Output RayTracingPass::setup(RenderGraph& graph)
{
    CHECK(graph.vulkan().supportsRayTracing(), "selected GPU does not support the ray tracing route");
    const auto& device   = graph.vulkan().physicalDeviceHandle();
    const auto  features = device.getFormatProperties(vk::Format::eR8G8B8A8Unorm).optimalTilingFeatures;
    CHECK(
        features & vk::FormatFeatureFlagBits::eStorageImage,
        "ray tracing output format does not support storage images"
    );

    const auto extent = graph.swapchain().extent();
    rayImage = graph.createImage({.format = vk::Format::eR8G8B8A8Unorm, .extent = {extent.width, extent.height, 1}});
    rayImage = graph.write(rayImage);
    graph.use(
        rayImage,
        {
            .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .access = vk::AccessFlagBits2::eShaderStorageWrite,
            .layout = vk::ImageLayout::eGeneral,
        }
    );
    if (!*pipeline)
        createPipeline(graph);
    return {rayImage};
}

void RayTracingPass::createPipeline(RenderGraph& graph)
{
    const auto& vulkan = graph.vulkan();
    const auto& device = vulkan.deviceHandle();
    const auto  properties =
        vulkan.physicalDeviceHandle()
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    const auto& limits = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    CHECK(
        limits.maxRayRecursionDepth >= kRayRecursionDepth,
        "ray tracing visibility requires recursion depth {}, GPU supports {}",
        kRayRecursionDepth,
        limits.maxRayRecursionDepth
    );
    CHECK(
        vulkan.properties().limits.maxPerStageDescriptorSamplers >= maxTextures &&
            vulkan.properties().limits.maxPerStageDescriptorSampledImages >= maxTextures,
        "ray tracing requires {} material texture descriptors",
        maxTextures
    );

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
            .binding         = 3,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = maxTextures,
            .stageFlags      = vk::ShaderStageFlagBits::eClosestHitKHR
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = 4,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = maxTextures,
            .stageFlags      = vk::ShaderStageFlagBits::eClosestHitKHR
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = 5,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eClosestHitKHR
        },
    };
    descriptorLayout           = vkCheck(device.createDescriptorSetLayout(
        {.bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data()}
    ));
    const std::array poolSizes = {
        vk::DescriptorPoolSize{
            .type            = vk::DescriptorType::eAccelerationStructureKHR,
            .descriptorCount = maxFramesInFlight
        },
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageImage, .descriptorCount = maxFramesInFlight},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = maxFramesInFlight * 2},
        vk::DescriptorPoolSize{
            .type            = vk::DescriptorType::eSampledImage,
            .descriptorCount = maxFramesInFlight * maxTextures
        },
        vk::DescriptorPoolSize{
            .type            = vk::DescriptorType::eSampler,
            .descriptorCount = maxFramesInFlight * maxTextures
        },
    };
    descriptorPool = vkCheck(device.createDescriptorPool(
        {.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
         .maxSets       = maxFramesInFlight,
         .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
         .pPoolSizes    = poolSizes.data()}
    ));
    for (auto& frame : frames)
        frame.set = std::move(
            vkCheck(device.allocateDescriptorSets(
                        {.descriptorPool = *descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &*descriptorLayout}
                    ))
                .front()
        );

    const vk::PushConstantRange parameterRange{
        .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR,
        .size       = sizeof(RayTracingParameters),
    };
    pipelineLayout = vkCheck(device.createPipelineLayout(
        {.setLayoutCount         = 1,
         .pSetLayouts            = &*descriptorLayout,
         .pushConstantRangeCount = 1,
         .pPushConstantRanges    = &parameterRange}
    ));
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
    };
    const vk::RayTracingPipelineCreateInfoKHR info{
        .stageCount                   = static_cast<uint32_t>(stages.size()),
        .pStages                      = stages.data(),
        .groupCount                   = static_cast<uint32_t>(groups.size()),
        .pGroups                      = groups.data(),
        .maxPipelineRayRecursionDepth = kRayRecursionDepth,
        .layout                       = *pipelineLayout,
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
    pipeline = vk::raii::Pipeline(device, rawPipeline);

    const vk::DeviceSize stride     = alignUp(limits.shaderGroupHandleSize, limits.shaderGroupHandleAlignment);
    const vk::DeviceSize missOffset = alignUp(stride, limits.shaderGroupBaseAlignment);
    const vk::DeviceSize hitOffset  = alignUp(missOffset + stride * kMissShaderCount, limits.shaderGroupBaseAlignment);
    CHECK(stride <= limits.maxShaderGroupStride, "shader binding table stride exceeds the device limit");
    bindingTable = Buffer(
        vulkan.physicalDeviceHandle(),
        device,
        hitOffset + stride + limits.shaderGroupBaseAlignment,
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
    // Group order: raygen, primary miss, visibility miss, closest hit.
    const std::array<vk::DeviceSize, 4> recordOffsets{0, missOffset, missOffset + stride, hitOffset};
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
    hitRegion = vk::StridedDeviceAddressRegionKHR{.deviceAddress = base + hitOffset, .stride = stride, .size = stride};
}

void RayTracingPass::prepareLights(RenderGraph& graph, Frame& frame) const
{
    const auto&          vulkan = graph.vulkan();
    const auto&          lights = graph.renderData().lights;
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

void RayTracingPass::prepareScene(RenderGraph& graph, Frame& frame)
{
    const auto& vulkan   = graph.vulkan();
    const auto& device   = vulkan.deviceHandle();
    auto&       commands = graph.commands();
    const auto& items    = graph.renderData().drawItems;
    CHECK(items.size() < (1u << 24), "ray tracing instance count exceeds the custom-index limit");
    static_assert(sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24);

    const vk::MemoryBarrier2 reuseBarrier{
        .srcStageMask  = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask  = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        .dstAccessMask =
            vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
    };
    commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &reuseBarrier});
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    std::vector<HitData>                            hits;
    std::map<const GpuMaterial*, uint32_t>          materialIndices;
    const auto white   = renderContext().resources->texture(BuiltinAssets::Texture::white);
    const auto sampler = renderContext().resources->sampler(BuiltinAssets::Sampler::linearRepeat);
    std::array<vk::DescriptorImageInfo, maxTextures> images, samplers;
    images.fill({.imageView = white->imageView(), .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal});
    samplers.fill({.sampler = sampler});
    const auto append = [&](const GpuMesh&   mesh,
                            uint32_t         firstIndex,
                            uint32_t         indexCount,
                            const glm::mat4& model,
                            const glm::vec4& color,
                            uint32_t         textureIndex)
    {
        CHECK(indexCount % 3 == 0, "ray tracing submesh requires triangle indices");
        const uint64_t                     vertexAddress = mesh.vertices().address(device);
        const uint64_t                     indexAddress  = mesh.indices().address(device);
        VkAccelerationStructureGeometryKHR triangles{
            .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR,
        };
        triangles.geometry.triangles = {
            .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData   = {.deviceAddress = vertexAddress},
            .vertexStride = sizeof(Vertex),
            .maxVertex    = mesh.vertexCount() - 1,
            .indexType    = VK_INDEX_TYPE_UINT32,
            .indexData    = {.deviceAddress = indexAddress + firstIndex * sizeof(uint32_t)},
        };
        const auto [bottom, created] = blas.try_emplace({&mesh, firstIndex, indexCount});
        if (created)
        {
            bottom->second.create(vulkan, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, triangles, indexCount / 3);
            bottom->second.build(vulkan, commands, triangles, indexCount / 3);
        }
        VkAccelerationStructureInstanceKHR instance{};
        for (uint32_t row = 0; row < 3; ++row)
            for (uint32_t column = 0; column < 4; ++column)
                instance.transform.matrix[row][column] = model[column][row];
        instance.instanceCustomIndex            = static_cast<uint32_t>(hits.size());
        instance.mask                           = 0xff;
        instance.flags                          = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = bottom->second.address();
        instances.push_back(instance);
        hits.push_back({vertexAddress, indexAddress, color, firstIndex, textureIndex});
    };
    for (const auto& item : items)
    {
        const auto [material, added] =
            materialIndices.try_emplace(item.material, static_cast<uint32_t>(materialIndices.size()));
        CHECK(material->second < maxTextures, "ray tracing supports at most {} materials per frame", maxTextures);
        if (added)
        {
            images[material->second] = vk::DescriptorImageInfo{
                .imageView   = item.material->baseColorView(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
            };
            samplers[material->second] = vk::DescriptorImageInfo{.sampler = item.material->baseColorSampler()};
        }
        append(
            *item.mesh,
            item.firstIndex,
            item.indexCount,
            item.model,
            item.material->baseColorFactor(),
            material->second
        );
    }
    buildBarrier(commands, false);

    const uint32_t count = static_cast<uint32_t>(instances.size());
    CHECK(
        std::max(count, 1u) * sizeof(HitData) <= vulkan.properties().limits.maxStorageBufferRange,
        "ray tracing hit data exceeds maxStorageBufferRange"
    );
    if (frame.instanceCount != count)
    {
        frame.tlas.reset();
        frame.instances.reset();
        frame.hitData.reset();
        const auto host = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        frame.instances = Buffer(
            vulkan.physicalDeviceHandle(),
            device,
            std::max(count, 1u) * sizeof(VkAccelerationStructureInstanceKHR),
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress,
            host
        );
        frame.hitData = Buffer(
            vulkan.physicalDeviceHandle(),
            device,
            std::max(count, 1u) * sizeof(HitData),
            vk::BufferUsageFlagBits::eStorageBuffer,
            host
        );
    }
    VkAccelerationStructureGeometryKHR top{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
    };
    top.geometry.instances = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .data  = {.deviceAddress = frame.instances.address(device)},
    };
    if (frame.instanceCount != count)
    {
        frame.tlas.create(vulkan, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, top, count);
        frame.instanceCount = count;
    }
    if (count)
    {
        frame.instances.upload(instances.data(), instances.size() * sizeof(instances.front()));
        frame.hitData.upload(hits.data(), hits.size() * sizeof(hits.front()));
    }
    frame.tlas.build(vulkan, commands, top, count);
    buildBarrier(commands, true);

    const vk::AccelerationStructureKHR                   tlas = frame.tlas.handle();
    const vk::WriteDescriptorSetAccelerationStructureKHR accelerationInfo{
        .accelerationStructureCount = 1,
        .pAccelerationStructures    = &tlas
    };
    const vk::DescriptorImageInfo output{
        .imageView   = graph.getImage(rayImage).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = vk::Format::eR8G8B8A8Unorm,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eGeneral
    };
    const vk::DescriptorBufferInfo hitInfo{.buffer = frame.hitData.handle(), .range = frame.hitData.size()};
    const vk::DescriptorBufferInfo lightInfo{.buffer = frame.lightData.handle(), .range = frame.lightData.size()};
    const std::array               writes = {
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
                          .dstBinding      = 3,
                          .descriptorCount = maxTextures,
                          .descriptorType  = vk::DescriptorType::eSampledImage,
                          .pImageInfo      = images.data()
        },
        vk::WriteDescriptorSet{
                          .dstSet          = *frame.set,
                          .dstBinding      = 4,
                          .descriptorCount = maxTextures,
                          .descriptorType  = vk::DescriptorType::eSampler,
                          .pImageInfo      = samplers.data()
        },
        vk::WriteDescriptorSet{
                          .dstSet          = *frame.set,
                          .dstBinding      = 5,
                          .descriptorCount = 1,
                          .descriptorType  = vk::DescriptorType::eStorageBuffer,
                          .pBufferInfo     = &lightInfo
        },
    };
    device.updateDescriptorSets(writes, {});
}

void RayTracingPass::execute(RenderGraph& graph)
{
    auto&  commands = graph.commands();
    Frame& frame    = frames[graph.frameIndex()];
    prepareLights(graph, frame);
    prepareScene(graph, frame);
    commands.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
    const std::array sets = {*frame.set};
    commands.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *pipelineLayout, 0, sets, {});
    const auto& view     = graph.frameData(graph.frameIndex()).viewUniforms();
    const auto& viewport = graph.view().viewport;
    commands.pushConstants<RayTracingParameters>(
        *pipelineLayout,
        vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR,
        0,
        RayTracingParameters{
            .inverseViewProjection = view.inverseViewProjection,
            .cameraPosition        = view.cameraPosition,
            .viewport              = {viewport.x, viewport.y, viewport.width, viewport.height},
            .lightCount            = static_cast<uint32_t>(graph.renderData().lights.size()),
        }
    );
    const auto extent = graph.swapchain().extent();
    commands.traceRaysKHR(raygenRegion, missRegion, hitRegion, {}, extent.width, extent.height, 1);
}
