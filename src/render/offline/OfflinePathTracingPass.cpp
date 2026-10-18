#include "render/offline/OfflinePathTracingPass.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
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
#include "render/raytracing/RayTracingMaterials.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuShader.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace
{
constexpr uint32_t kMissShaderCount = 2;
constexpr uint32_t kHitShaderCount  = 2;
vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

OfflinePathTracingPass::Output OfflinePathTracingPass::setup(RenderGraph& graph, const PassSetup& setup)
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

    const auto blas       = graph.addPass(bottomLevels, setup);
    const auto structures = graph.addPass(topLevel, setup, blas);
    for (const auto structure : {structures.blas, structures.tlas})
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
    const vk::DeviceSize accumulationBytes = vk::DeviceSize(extent.width) * extent.height * sizeof(glm::vec4);
    CHECK(
        accumulationBytes <= setup.vulkan.properties().limits.maxStorageBufferRange,
        "offline accumulation buffer exceeds maxStorageBufferRange: {} bytes",
        accumulationBytes
    );
    if (accumulation.size() != accumulationBytes)
    {
        accumulation = Buffer(
            setup.vulkan.physicalDeviceHandle(),
            setup.vulkan.deviceHandle(),
            accumulationBytes,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
    }
    if (accumulationExtent != extent)
    {
        accumulationExtent = extent;
        resetAccumulation();
    }
    accumulationHandle = graph.importBuffer(
        accumulation,
        {.stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
         .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite}
    );
    accumulationHandle = graph.write(accumulationHandle);
    graph.use(
        accumulationHandle,
        {.stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
         .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite}
    );

    const auto maxRecursionDepth = context().config->rendererConfig().feature.offline.maxRecursionDepth;
    if (!*pipeline || pipelineRecursionDepth != maxRecursionDepth)
        createPipeline(setup, maxRecursionDepth);
    return {rayImage};
}

void OfflinePathTracingPass::createPipeline(const PassSetup& setup, uint32_t maxRecursionDepth)
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
    CHECK(
        vulkan.properties().limits.maxPerStageDescriptorSamplers >= kMaxRayTracingSamplers &&
            vulkan.properties().limits.maxPerStageDescriptorSampledImages >= kMaxRayTracingTextures,
        "ray tracing requires {} texture and {} sampler descriptors",
        kMaxRayTracingTextures,
        kMaxRayTracingSamplers
    );

    if (!*pipelineLayout)
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
                .binding         = 3,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .descriptorCount = kMaxRayTracingTextures,
                .stageFlags      = vk::ShaderStageFlagBits::eClosestHitKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = 4,
                .descriptorType  = vk::DescriptorType::eSampler,
                .descriptorCount = kMaxRayTracingSamplers,
                .stageFlags      = vk::ShaderStageFlagBits::eClosestHitKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = 5,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eClosestHitKHR
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = 6,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eClosestHitKHR,
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = 7,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR,
            },
        };
        descriptorLayout           = vkCheck(device.createDescriptorSetLayout(
            {.bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data()}
        ));
        const std::array poolSizes = {
            vk::DescriptorPoolSize{.type = vk::DescriptorType::eAccelerationStructureKHR, .descriptorCount = 1},
            vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageImage, .descriptorCount = 1},
            vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = 4},
            vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampledImage, .descriptorCount = kMaxRayTracingTextures},
            vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampler, .descriptorCount = kMaxRayTracingSamplers},
        };
        descriptorPool = vkCheck(device.createDescriptorPool(
            {.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
             .maxSets       = 1,
             .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
             .pPoolSizes    = poolSizes.data()}
        ));
        frame.set      = std::move(
            vkCheck(device.allocateDescriptorSets(
                        {.descriptorPool = *descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &*descriptorLayout}
                    ))
                .front()
        );

        const vk::PushConstantRange parameterRange{
            .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR,
            .size       = sizeof(OfflineParameters),
        };
        pipelineLayout = vkCheck(device.createPipelineLayout(
            {.setLayoutCount         = 1,
             .pSetLayouts            = &*descriptorLayout,
             .pushConstantRangeCount = 1,
             .pPushConstantRanges    = &parameterRange}
        ));
    }

    GpuShader        shader(device, "shaders/offline_pathtracing.spv");
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

void OfflinePathTracingPass::prepareLights(const PassFrame& execution, Frame& frame) const
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

void OfflinePathTracingPass::prepareBindings(RenderGraph& graph, const PassFrame& execution, Frame& frame)
{
    const auto& vulkan = execution.setup.vulkan;
    const auto& device = vulkan.deviceHandle();
    const auto& items  = execution.renderData.drawItems;
    static_assert(sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24);

    RayTracingMaterials            materials{*renderContext().resources};
    std::vector<RayTracingHitData> hits;
    hits.reserve(items.size());
    for (const auto& item : items)
        hits.push_back({
            item.mesh->vertices().address(device),
            item.mesh->indices().address(device),
            item.firstIndex,
            materials.add(*item.material),
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
    materials.upload(frame.materialData, vulkan);

    const vk::AccelerationStructureKHR                   tlas = topLevel.structure(execution.frameIndex).handle();
    const vk::WriteDescriptorSetAccelerationStructureKHR accelerationInfo{
        .accelerationStructureCount = 1,
        .pAccelerationStructures    = &tlas
    };
    const vk::DescriptorImageInfo output{
        .imageView = graph.getImage(rayImage, execution.frameIndex)
                         .getOrCreateImageView({
                             .viewType = vk::ImageViewType::e2D,
                             .format   = graph.imageInfo(rayImage).format,
                             .subresourceRange =
                                 {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
                         }),
        .imageLayout = vk::ImageLayout::eGeneral
    };
    const vk::DescriptorBufferInfo hitInfo{.buffer = frame.hitData.handle(), .range = frame.hitData.size()};
    const vk::DescriptorBufferInfo lightInfo{.buffer = frame.lightData.handle(), .range = frame.lightData.size()};
    const vk::DescriptorBufferInfo materialInfo{.buffer = frame.materialData.handle(), .range = frame.materialData.size()};
    const vk::DescriptorBufferInfo accumulationInfo{.buffer = accumulation.handle(), .range = accumulation.size()};
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
                          .descriptorCount = kMaxRayTracingTextures,
                          .descriptorType  = vk::DescriptorType::eSampledImage,
                          .pImageInfo      = materials.images.data()
        },
        vk::WriteDescriptorSet{
                          .dstSet          = *frame.set,
                          .dstBinding      = 4,
                          .descriptorCount = kMaxRayTracingSamplers,
                          .descriptorType  = vk::DescriptorType::eSampler,
                          .pImageInfo      = materials.samplers.data()
        },
        vk::WriteDescriptorSet{
                          .dstSet          = *frame.set,
                          .dstBinding      = 5,
                          .descriptorCount = 1,
                          .descriptorType  = vk::DescriptorType::eStorageBuffer,
                          .pBufferInfo     = &lightInfo
        },
        vk::WriteDescriptorSet{
                          .dstSet          = *frame.set,
                          .dstBinding      = 6,
                          .descriptorCount = 1,
                          .descriptorType  = vk::DescriptorType::eStorageBuffer,
                          .pBufferInfo     = &materialInfo
        },
    };
    device.updateDescriptorSets(writes, {});
    device.updateDescriptorSets(
        vk::WriteDescriptorSet{
            .dstSet          = *frame.set,
            .dstBinding      = 7,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &accumulationInfo
        },
        {}
    );
}

void OfflinePathTracingPass::execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution)
{
    auto& commands = commandBuffer;
    DCHECK(execution.frameIndex == 0);
    CHECK(accumulatedSamples < std::numeric_limits<uint32_t>::max());
    prepareLights(execution, frame);
    prepareBindings(graph, execution, frame);
    commands.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
    const std::array sets = {*frame.set};
    commands.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *pipelineLayout, 0, sets, {});
    const auto& view     = execution.data.viewUniforms();
    const auto& viewport = execution.view.viewport;
    commands.pushConstants<OfflineParameters>(
        *pipelineLayout,
        vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR,
        0,
        OfflineParameters{
            .inverseViewProjection = view.inverseViewProjection,
            .cameraPosition        = view.cameraPosition,
            .viewport              = {viewport.x, viewport.y, viewport.width, viewport.height},
            .lightRanges           = execution.renderData.lightRanges,
            .maxRecursionDepth     = pipelineRecursionDepth,
            .sampleIndex           = accumulatedSamples,
        }
    );
    const auto extent = execution.setup.swapchain.extent();
    commands.traceRaysKHR(raygenRegion, missRegion, hitRegion, {}, extent.width, extent.height, 1);
    ++accumulatedSamples;
}

void OfflinePathTracingPass::resetAccumulation() noexcept
{
    accumulatedSamples = 0;
    previousScene.reset();
}

void OfflinePathTracingPass::updateScene(const RenderView& view, const SceneRenderData& scene, uint64_t assetRevision)
{
    const bool cameraChanged = view.uniforms.inverseViewProjection != previousView.uniforms.inverseViewProjection ||
        view.uniforms.cameraPosition != previousView.uniforms.cameraPosition ||
        view.viewport.x != previousView.viewport.x || view.viewport.y != previousView.viewport.y ||
        view.viewport.width != previousView.viewport.width || view.viewport.height != previousView.viewport.height;
    if (!previousScene || *previousScene != scene || cameraChanged || previousAssetRevision != assetRevision)
        accumulatedSamples = 0;
    previousScene         = scene;
    previousView          = view;
    previousAssetRevision = assetRevision;
}
