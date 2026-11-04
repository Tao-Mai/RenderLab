#include "render/raytracing/realtime/RayTracingPass.h"

#include "core/Context.h"
#include "core/Logger.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/device/Device.h"
#include "render/device/GpuProfiler.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/graph/RenderGraph.h"
#include "render/resources/GpuShader.h"
#include "render/resources/MaterialTable.h"

#include <algorithm>
#include <cstring>
#include <bit>
#include <functional>

namespace
{
constexpr uint32_t kRaygenShaderCount = 3;
constexpr uint32_t kMissShaderCount   = 2;
constexpr uint32_t kHitShaderCount    = 2;
vk::DeviceSize     alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

RayTracingPass::Output RayTracingPass::setup(
    RenderGraph&                graph,
    const PassSetup&            setup,
    TlasBuildPass::Output       scene,
    RayTracingScenePass::Output preparedGeometry,
    RenderGraph::BufferHandle   lights
)
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
    geometry  = preparedGeometry;
    for (const auto buffer : {geometry.objects, geometry.triangles, lights})
        graph.use(
            buffer,
            {
                .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .access = vk::AccessFlagBits2::eShaderStorageRead,
            }
        );
    renderContext().materialTable->use(graph, vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
    for (const auto structure : {scene.blas, scene.tlas})
        graph.use(
            structure,
            {
                .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .access = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            }
        );

    extent                            = setup.renderExtent;
    const vk::DeviceSize pixelCount   = vk::DeviceSize(extent.width) * extent.height;
    const auto           createBuffer = [&](vk::DeviceSize size)
    {
        CHECK(
            size <= setup.vulkan.properties().limits.maxStorageBufferRange,
            "ReSTIR buffer requires {} bytes; GPU storage-buffer limit is {}",
            size,
            setup.vulkan.properties().limits.maxStorageBufferRange
        );
        auto buffer = graph.write(graph.createBuffer({.size = size, .usage = vk::BufferUsageFlagBits::eStorageBuffer}));
        graph.use(
            buffer,
            {
                .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            }
        );
        return buffer;
    };
    for (auto& buffer : restirSurfaces)
    {
        buffer = createBuffer(pixelCount * sizeof(RestirSurfaceData));
    }
    initialReservoirs = createBuffer(pixelCount * sizeof(RestirReservoirData));
    reservoirHistory  = createBuffer(pixelCount * sizeof(RestirReservoirData) * 2);
    restirParameters  = graph.createBuffer({
         .size  = sizeof(RestirFrameParameters),
         .usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    graph.use(
        restirParameters,
        {
            .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .access = vk::AccessFlagBits2::eUniformRead,
        }
    );
    writeLayer       = 0;
    hasRestirHistory = false;

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
    output                = {};
    const bool enableSvgf = setup.config.feature.rayTracing.svgf;
    if (enableSvgf)
    {
        output.direct   = createOutput(kHdrColorFormat);
        output.indirect = createOutput(kHdrColorFormat);
        output.emission = createOutput(kHdrColorFormat);
        output.albedo   = createOutput(kHdrColorFormat);
    }
    else
        output.rayImage = createOutput(kHdrColorFormat);
    output.depth       = createOutput(vk::Format::eR32Sfloat);
    output.worldNormal = createOutput(vk::Format::eR16G16B16A16Sfloat);
    sampleIndex        = 0;
    svgfEnabled        = enableSvgf;
    for (const bool variantSvgf : {false, true})
    {
        if (!*variants[variantSvgf].pipeline)
        {
            createPipeline(setup, variantSvgf);
        }
    }

    // A graph rebuild destroys the images referenced by the previous descriptor sets.
    for (auto& frame : frames)
    {
        frame.set = nullptr;
        frame.set = renderContext().device->allocateDescriptorSet(variants[svgfEnabled].descriptorSetLayout);
        frame.bufferRevisions = {};
        frame.surfaceLayer    = ~0u;
        frame.tlasRevision    = 0;
    }
    return output;
}

void RayTracingPass::createPipeline(const PassSetup& setup, bool enableSvgf)
{
    auto&       variant = variants[enableSvgf];
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
        for (uint32_t binding = RealtimeRayTracingBinding::kSurfaces;
             binding <= RealtimeRayTracingBinding::kRestirFrame;
             ++binding)
        {
            bindings.push_back({
                .binding         = binding,
                .descriptorType  = binding == RealtimeRayTracingBinding::kRestirFrame
                     ? vk::DescriptorType::eUniformBuffer
                     : vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR,
            });
        }
        if (!enableSvgf)
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

    GpuShader            shader(device, enableSvgf ? "shaders/raytracing.svgf.spv" : "shaders/raytracing.spv");
    constexpr std::array kEntryPoints{
        "raygenMain",
        "temporalMain",
        "spatialMain",
        "missMain",
        "visibilityMissMain",
        "closestHitMain",
        "lightClosestHitMain"
    };
    std::array<vk::PipelineShaderStageCreateInfo, kEntryPoints.size()>      stages;
    std::array<vk::RayTracingShaderGroupCreateInfoKHR, kEntryPoints.size()> groups;
    for (uint32_t index = 0; index < kEntryPoints.size(); ++index)
    {
        const bool hit = index >= kRaygenShaderCount + kMissShaderCount;
        stages[index]  = vk::PipelineShaderStageCreateInfo{
             .stage  = hit                     ? vk::ShaderStageFlagBits::eClosestHitKHR
                  : index < kRaygenShaderCount ? vk::ShaderStageFlagBits::eRaygenKHR
                                               : vk::ShaderStageFlagBits::eMissKHR,
             .module = shader.handle(),
             .pName  = kEntryPoints[index],
        };
        groups[index] = vk::RayTracingShaderGroupCreateInfoKHR{
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
    const vk::DeviceSize missOffset   = raygenStride * kRaygenShaderCount;
    const vk::DeviceSize hitOffset = alignUp(missOffset + stride * kMissShaderCount, limits.shaderGroupBaseAlignment);
    CHECK(stride <= limits.maxShaderGroupStride, "shader binding table stride exceeds the device limit");
    variant.bindingTable = Buffer(
        vulkan.physicalDeviceHandle(),
        device,
        hitOffset + stride * kHitShaderCount + limits.shaderGroupBaseAlignment,
        vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );
    const vk::DeviceAddress address     = variant.bindingTable.address(device);
    const vk::DeviceAddress base        = alignUp(address, limits.shaderGroupBaseAlignment);
    const uint32_t          kGroupCount = static_cast<uint32_t>(groups.size());
    std::vector<uint8_t>    handles(limits.shaderGroupHandleSize * kGroupCount);
    vkCheck(
        static_cast<vk::Result>(device.getDispatcher()->vkGetRayTracingShaderGroupHandlesKHR(
            static_cast<VkDevice>(*device),
            static_cast<VkPipeline>(*variant.pipeline),
            0,
            kGroupCount,
            handles.size(),
            handles.data()
        ))
    );
    std::vector<uint8_t> records(variant.bindingTable.size());
    // Three raygen records, two miss records, then the surface and rectangle hit records.
    const std::array<vk::DeviceSize, 7> recordOffsets{
        0,
        raygenStride,
        raygenStride * 2,
        missOffset,
        missOffset + stride,
        hitOffset,
        hitOffset + stride
    };
    for (uint32_t group = 0; group < kGroupCount; ++group)
        std::memcpy(
            records.data() + base - address + recordOffsets[group],
            handles.data() + limits.shaderGroupHandleSize * group,
            limits.shaderGroupHandleSize
        );
    variant.bindingTable.upload(records.data(), records.size());
    for (uint32_t index = 0; index < kRaygenShaderCount; ++index)
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

void RayTracingPass::updateBufferDescriptors(RenderGraph& graph, const PassFrame& execution, Frame& frame)
{
    const std::array handles{
        geometry.objects,
        geometry.triangles,
        restirSurfaces[writeLayer],
        restirSurfaces[writeLayer ^ 1u],
        initialReservoirs,
        reservoirHistory,
        restirParameters
    };
    const std::array bindings{
        RealtimeRayTracingBinding::kObjects,
        RealtimeRayTracingBinding::kTriangles,
        RealtimeRayTracingBinding::kSurfaces,
        RealtimeRayTracingBinding::kPreviousSurfaces,
        RealtimeRayTracingBinding::kInitialReservoirs,
        RealtimeRayTracingBinding::kReservoirHistory,
        RealtimeRayTracingBinding::kRestirFrame
    };
    std::array<vk::DescriptorBufferInfo, 7> infos;
    std::array<vk::WriteDescriptorSet, 7>   writes;
    uint32_t                                writeCount = 0;
    for (uint32_t index = 0; index < handles.size(); ++index)
    {
        const auto revision     = graph.bufferRevision(handles[index]);
        const bool changedLayer = (index == 2 || index == 3) && frame.surfaceLayer != writeLayer;
        if (frame.bufferRevisions[index] == revision && !changedLayer)
        {
            continue;
        }
        const auto& buffer   = graph.getBuffer(handles[index]);
        infos[index]         = vk::DescriptorBufferInfo{.buffer = buffer.handle(), .range = buffer.size()};
        writes[writeCount++] = {
            .dstSet          = *frame.set,
            .dstBinding      = bindings[index],
            .descriptorCount = 1,
            .descriptorType =
                index + 1 == handles.size() ? vk::DescriptorType::eUniformBuffer : vk::DescriptorType::eStorageBuffer,
            .pBufferInfo = &infos[index],
        };
        frame.bufferRevisions[index] = revision;
    }
    execution.setup.vulkan.deviceHandle().updateDescriptorSets(std::span{writes.data(), writeCount}, {});
    frame.surfaceLayer = writeLayer;
}

void RayTracingPass::updateTlasDescriptor(RenderGraph& graph, const PassFrame& execution, Frame& frame)
{
    const auto& device    = execution.setup.vulkan.deviceHandle();
    const auto& structure = graph.getAccelerationStructure(sceneTlas);
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

void RayTracingPass::writeDescriptorSet(RenderGraph& graph, const PassSetup& setup)
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

void RayTracingPass::prepare(RenderGraph& graph, const PassFrame& execution)
{
    CpuProfileScope timing{context().profiler, "RT.Buffer preparation"};
    auto&           frame = frames[execution.frameIndex];
    prepareRestir(graph, execution);
    updateBufferDescriptors(graph, execution, frame);
}

void RayTracingPass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution)
{
    const auto& variant = variants[svgfEnabled];
    auto&  commands = commandBuffer;
    Frame& frame    = frames[execution.frameIndex];
    {
        CpuProfileScope timing{context().profiler, "RT.Binding preparation"};
        GpuProfileScope gpuTiming{execution.gpuProfiler, commands, "RT.Binding preparation"};
        updateTlasDescriptor(graph, execution, frame);
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
            .viewport       = {viewport.x, viewport.y, viewport.width, viewport.height},
            .lightRanges    = execution.renderData.lightRanges,
            .maxBounces     = execution.setup.config.feature.rayTracing.maxBounces,
            .sampleIndex    = sampleIndex++,
            .iblEnabled     = execution.setup.config.feature.ibl && execution.renderData.environmentMap ? 1u : 0u,
            .candidateCount = execution.setup.config.feature.restir.candidateCount,
        }
    );
    constexpr std::array kStageNames{"RT.Initial RIS and paths", "ReSTIR.Temporal", "ReSTIR.Spatial and shading"};
    for (uint32_t stage = 0; stage < kRaygenShaderCount; ++stage)
    {
        if (stage > 0)
        {
            const vk::MemoryBarrier2 barrier{
                .srcStageMask  = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                .dstStageMask  = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            };
            commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
        }
        GpuProfileScope timing{execution.gpuProfiler, commands, kStageNames[stage]};
        commands.traceRaysKHR(
            variant.raygenRegions[stage],
            variant.missRegion,
            variant.hitRegion,
            {},
            extent.width,
            extent.height,
            1
        );
    }
    writeLayer ^= 1u;
    hasRestirHistory = true;
}

void RayTracingPass::prepareRestir(RenderGraph& graph, const PassFrame& execution)
{
    const auto&     data     = execution.renderData;
    const auto&     config   = execution.setup.config.feature;
    const auto&     viewport = execution.view.viewport;
    const glm::vec4 currentViewport{viewport.x, viewport.y, viewport.width, viewport.height};
    const bool      ibl              = config.ibl && data.environmentMap.has_value();
    const auto      materialRevision = renderContext().materialTable->revision();
    uint64_t        signature        = std::hash<std::string>{}(data.sceneId.value);
    const auto      combine          = [&signature](uint64_t value)
    {
        constexpr uint64_t kHashGoldenRatio = 0x9e3779b97f4a7c15ull;
        signature ^= value + kHashGoldenRatio + (signature << 6) + (signature >> 2);
    };
    for (uint32_t index = 0; index < data.drawItems.size(); ++index)
    {
        const auto& item = data.drawItems[index];
        combine(std::hash<std::string>{}(item.mesh.value));
        combine(item.submeshIndex);
        combine(item.selectionId);
        combine(data.materialIndices[index]);
        for (uint32_t column = 0; column < 4; ++column)
        {
            for (uint32_t row = 0; row < 4; ++row)
            {
                combine(std::bit_cast<uint32_t>(item.model[column][row]));
            }
        }
    }
    const bool historyValid = hasRestirHistory && signature == previousSceneSignature &&
        materialRevision == previousMaterialRevision && data.lights == previousLights &&
        data.environmentMap == previousEnvironment && config.restir == previousRestir &&
        config.rayTracing.maxBounces == previousMaxBounces && ibl == previousIbl;
    const RestirFrameParameters parameters{
        .previousViewProjection = previousViewProjection,
        .previousViewport       = previousViewport,
        .previousDepthRow       = previousDepthRow,
        .width                  = extent.width,
        .height                 = extent.height,
        .writeLayer             = writeLayer,
        .historyValid           = historyValid ? 1u : 0u,
        .maxHistoryLength       = config.restir.maxHistoryLength,
        .spatialSampleCount     = config.restir.spatialSampleCount,
        .spatialRadius          = config.restir.spatialRadius,
        .temporalReuse          = config.restir.temporalReuse ? 1u : 0u,
        .spatialReuse           = config.restir.spatialReuse ? 1u : 0u,
    };
    const auto upload = graph.allocateUpload(restirParameters, sizeof(parameters));
    std::memcpy(upload.data.data(), &parameters, sizeof(parameters));

    const auto& view         = execution.view.uniforms.view;
    previousViewProjection   = execution.view.uniforms.viewProjection;
    previousViewport         = currentViewport;
    previousDepthRow         = -glm::vec4{view[0][2], view[1][2], view[2][2], view[3][2]};
    previousSceneSignature   = signature;
    previousMaterialRevision = materialRevision;
    previousLights           = data.lights;
    previousEnvironment      = data.environmentMap;
    previousRestir           = config.restir;
    previousMaxBounces       = config.rayTracing.maxBounces;
    previousIbl              = ibl;
}
