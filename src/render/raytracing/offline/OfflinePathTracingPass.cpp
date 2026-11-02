#include "render/raytracing/offline/OfflinePathTracingPass.h"

#include "asset/AssetManager.h"
#include "asset/MeshAsset.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Vertex.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/device/Device.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/graph/RenderGraph.h"
#include "render/resources/GpuMesh.h"
#include "render/resources/GpuShader.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <chrono>
#include <format>
#include <cstring>
#include <limits>

namespace
{
constexpr uint32_t kMissShaderCount        = 2;
constexpr uint32_t kHitShaderCount         = 1;
constexpr uint32_t kMinBdptShaderDepth     = 4;
constexpr float    kMinimumSceneRadius     = 0.001f;
constexpr float    kSceneBoundsPadding     = 1.001f;
constexpr uint32_t kBdptTileSize           = 128;
constexpr uint32_t kBdptTileReferenceDepth = 6;
constexpr auto     kParameterStages        = vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute;

uint32_t shaderDepth(const RenderFeature::Offline& settings)
{
    return settings.algorithm == OfflineAlgorithm::BDPT
        ? std::bit_ceil(std::max(kMinBdptShaderDepth, settings.bdpt.maxBounces))
        : 0;
}

constexpr std::array kSceneBufferBindings{
    OfflineRayTracingBinding::kMaterials,
    OfflineRayTracingBinding::kTextures,
    OfflineRayTracingBinding::kAreaEmissions,
    OfflineRayTracingBinding::kPoints,
    OfflineRayTracingBinding::kSpots,
    OfflineRayTracingBinding::kDirectionals,
    OfflineRayTracingBinding::kTriangles,
    OfflineRayTracingBinding::kEnvironments,
    OfflineRayTracingBinding::kProjections,
    OfflineRayTracingBinding::kGoniometrics,
    OfflineRayTracingBinding::kPortals,
};

constexpr std::array kSceneBufferSizes{
    sizeof(OfflineMaterialPreview),
    sizeof(OfflineTextureInstruction),
    sizeof(OfflineLightEmission),
    sizeof(OfflinePointLight),
    sizeof(OfflineSpotLight),
    sizeof(OfflineDirectionalLight),
    sizeof(OfflineLightTriangle),
    sizeof(OfflineEnvironmentLight),
    sizeof(OfflineProjectionLight),
    sizeof(OfflineGoniometricLight),
    sizeof(glm::vec4),
};

constexpr auto kSceneBufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;

vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

OfflinePathTracingPass::Output
OfflinePathTracingPass::setup(RenderGraph& graph, const PassSetup& setup, TlasBuildPass::Output scene)
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
    hitData   = graph.createBuffer({
          .size  = sizeof(OfflineHitData),
          .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });

    for (size_t index = 0; index < sceneBuffers.size(); ++index)
        sceneBuffers[index] = graph.createBuffer({.size = kSceneBufferSizes[index], .usage = kSceneBufferUsage});

    lightDistributionHandle =
        graph.createBuffer({.size = sizeof(OfflineLightDistributionEntry), .usage = kSceneBufferUsage});
    graph.use(
        lightDistributionHandle,
        {
            .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .access = vk::AccessFlagBits2::eShaderStorageRead,
        }
    );

    lightDistributionRevision = 0;
    sceneBufferRevisions.fill(0);
    uploadedSceneRevision = 0;

    graph.use(
        hitData,
        {.stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR, .access = vk::AccessFlagBits2::eShaderStorageRead}
    );
    for (const auto buffer : sceneBuffers)
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

    const auto extent = setup.renderExtent;
    rayImage          = graph.createImage({.format = kHdrColorFormat, .extent = {extent.width, extent.height, 1}});
    rayImage          = graph.write(rayImage);
    graph.use(
        rayImage,
        {
            .stages = vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eShaderStorageWrite,
            .layout = vk::ImageLayout::eGeneral,
        }
    );

    const vk::DeviceSize accumulationBytes =
        vk::DeviceSize(extent.width) * extent.height * sizeof(OfflineAccumulatedPixel);
    CHECK(
        accumulationBytes <= setup.vulkan.properties().limits.maxStorageBufferRange,
        "offline accumulation buffer exceeds maxStorageBufferRange: {} bytes",
        accumulationBytes
    );

    accumulationHandle = graph.createBuffer(
        {.size  = accumulationBytes,
         .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst}
    );
    accumulationHandle = graph.write(accumulationHandle);
    graph.use(
        accumulationHandle,
        {.stages = vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eRayTracingShaderKHR |
             vk::PipelineStageFlagBits2::eComputeShader,
         .access = vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageRead |
             vk::AccessFlagBits2::eShaderStorageWrite}
    );

    // PT only needs a bound placeholder. BDPT stores cumulative light-tracing splats here.
    const auto splatBytes =
        setup.config.feature.offline.algorithm == OfflineAlgorithm::BDPT ? accumulationBytes : sizeof(glm::uvec4);
    splatHandle = graph.write(graph.createBuffer({
        .size  = splatBytes,
        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    }));
    graph.use(
        splatHandle,
        {
            .stages = vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eRayTracingShaderKHR |
                vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageRead |
                vk::AccessFlagBits2::eShaderStorageWrite,
        }
    );

    if (!activePipeline || pipelineAlgorithm != setup.config.feature.offline.algorithm ||
        pipelineDepth != shaderDepth(setup.config.feature.offline))
        preparePipeline(setup);

    frame.hitBufferRevision = 0;
    frame.tlasRevision      = 0;

    accumulatedSamples  = 0;
    accumulatedProgress = 0;
    tileIndex           = 0;
    processedPixels     = 0;
    resetAccumulation   = true;
    return {rayImage};
}

void OfflinePathTracingPass::preparePipeline(const PassSetup& setup)
{
    pipelineAlgorithm = setup.config.feature.offline.algorithm;
    pipelineDepth     = shaderDepth(setup.config.feature.offline);

    const auto& vulkan = setup.vulkan;

    const auto& device = vulkan.deviceHandle();

    if (!pipelineLayout)
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {.binding         = OfflineRayTracingBinding::kTlas,
             .descriptorType  = vk::DescriptorType::eAccelerationStructureKHR,
             .descriptorCount = 1,
             .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR},
            {.binding         = OfflineRayTracingBinding::kObjects,
             .descriptorType  = vk::DescriptorType::eStorageBuffer,
             .descriptorCount = 1,
             .stageFlags      = vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eRaygenKHR},
        };
        for (const auto binding : kSceneBufferBindings)
            bindings.push_back(
                {.binding         = binding,
                 .descriptorType  = vk::DescriptorType::eStorageBuffer,
                 .descriptorCount = 1,
                 .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR}
            );
        bindings.push_back(
            {.binding         = OfflineRayTracingBinding::kLightDistribution,
             .descriptorType  = vk::DescriptorType::eStorageBuffer,
             .descriptorCount = 1,
             .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR}
        );
        bindings.push_back(
            {.binding         = OfflineRayTracingBinding::kAccumulation,
             .descriptorType  = vk::DescriptorType::eStorageBuffer,
             .descriptorCount = 1,
             .stageFlags      = kParameterStages}
        );
        bindings.push_back(
            {.binding         = OfflineRayTracingBinding::kSplats,
             .descriptorType  = vk::DescriptorType::eStorageBuffer,
             .descriptorCount = 1,
             .stageFlags      = kParameterStages}
        );
        bindings.push_back(
            {.binding         = OfflineRayTracingBinding::kColor,
             .descriptorType  = vk::DescriptorType::eStorageImage,
             .descriptorCount = 1,
             .stageFlags      = vk::ShaderStageFlagBits::eCompute}
        );

        auto& renderDevice  = *renderContext().device;
        descriptorSetLayout = renderDevice.createDescriptorSetLayout(bindings);
        frame.set           = renderDevice.allocateDescriptorSet(descriptorSetLayout);

        const vk::PushConstantRange parameterRange{
            .stageFlags = kParameterStages,
            .size       = sizeof(OfflinePathParameters),
        };
        emptySetLayout = renderDevice.createDescriptorSetLayout({});
        const std::array descriptorSetLayouts{
            &renderDevice.sceneDescriptorSetLayout,
            &emptySetLayout,
            &renderDevice.materialDescriptorSetLayout,
            &descriptorSetLayout
        };
        pipelineLayout = renderDevice.getOrCreatePipelineLayout(descriptorSetLayouts, std::array{parameterRange});
    }

    const bool bdpt    = pipelineAlgorithm == OfflineAlgorithm::BDPT;
    auto&      resolve = bdpt ? bdptResolvePipeline : ptResolvePipeline;
    if (!*resolve)
    {
        GpuShader shader(device, bdpt ? "shaders/offline_resolve.1.spv" : "shaders/offline_resolve.0.spv");
        resolve = vkCheck(device.createComputePipeline(
            nullptr,
            {
                .stage =
                    {.stage = vk::ShaderStageFlagBits::eCompute, .module = shader.handle(), .pName = "computeMain"},
                .layout = pipelineLayout,
            }
        ));
    }

    auto [entry, inserted] = pipelines.try_emplace({pipelineAlgorithm, pipelineDepth});
    auto& build            = entry->second;
    if (inserted)
    {
        if (bdpt)
        {
            build.future = std::async(
                std::launch::async,
                [&vulkan, layout = pipelineLayout, algorithm = pipelineAlgorithm, depth = pipelineDepth]
                { return createPipeline(vulkan, layout, algorithm, depth); }
            );
        }
        else
        {
            build.result.emplace(createPipeline(vulkan, pipelineLayout, pipelineAlgorithm, pipelineDepth));
        }
    }

    if (build.future.valid() && build.future.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
        build.result.emplace(build.future.get());

    activePipeline = build.result ? &*build.result : nullptr;
}

OfflinePathTracingPass::PipelineVariant OfflinePathTracingPass::createPipeline(
    const VulkanContext& vulkan, vk::PipelineLayout layout, OfflineAlgorithm algorithm, uint32_t depth
)
{
    PipelineVariant result;
    auto&           pipeline     = result.trace;
    auto&           bindingTable = result.bindingTable;
    auto&           raygenRegion = result.raygen;
    auto&           missRegion   = result.miss;
    auto&           hitRegion    = result.hit;

    const auto& device = vulkan.deviceHandle();
    const auto  properties =
        vulkan.physicalDeviceHandle()
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    const auto& limits = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    CHECK(kRayPipelineRecursionDepth <= limits.maxRayRecursionDepth);

    const bool bdpt = algorithm == OfflineAlgorithm::BDPT;
    GpuShader  shader(
        device,
        bdpt ? std::format("shaders/offline_bdpt.depth{}.spv", depth) : "shaders/offline_pathtracing.spv"
    );

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
        .maxPipelineRayRecursionDepth = kRayPipelineRecursionDepth,
        .layout                       = layout,
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
    // Group order: raygen, path miss, visibility miss, surface hit.
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
    hitRegion = vk::StridedDeviceAddressRegionKHR{
        .deviceAddress = base + hitOffset,
        .stride        = stride,
        .size          = stride * kHitShaderCount
    };
    return result;
}

void OfflinePathTracingPass::prepareHits(RenderGraph& graph, const PassFrame& execution, Frame& frame)
{
    const auto& device    = execution.setup.vulkan.deviceHandle();
    auto&       resources = *renderContext().resources;
    static_assert(
        sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24 &&
        offsetof(Vertex, tangent) == 32
    );

    const auto& items  = execution.renderData.drawItems;
    const auto  bytes  = std::max<size_t>(items.size(), 1) * sizeof(OfflineHitData);
    const auto  upload = graph.allocateUpload(hitData, bytes);
    auto*       hits   = reinterpret_cast<OfflineHitData*>(upload.data.data());
    for (uint32_t index = 0; index < items.size(); ++index)
    {
        const auto& item = items[index];
        const auto& mesh = resources.mesh(item.mesh);
        const auto& draw = execution.renderData.offlineScene->draws[index];
        const auto& part = mesh.submeshes()[item.submeshIndex];
        std::construct_at(
            hits + index,
            OfflineHitData{
                .vertexAddress = mesh.vertices().address(device),
                .indexAddress  = mesh.indices().address(device),
                .material      = {part.firstIndex, draw.materialIndex, draw.reverseOrientation, draw.firstAreaLight},
            }
        );
    }
    if (items.empty())
        std::construct_at(hits, OfflineHitData{});

    const auto& buffer = graph.getBuffer(hitData);
    if (frame.hitBufferRevision != graph.bufferRevision(hitData))
    {
        const vk::DescriptorBufferInfo info{.buffer = buffer.handle(), .range = buffer.size()};
        const vk::WriteDescriptorSet   write{
              .dstSet          = *frame.set,
              .dstBinding      = OfflineRayTracingBinding::kObjects,
              .descriptorCount = 1,
              .descriptorType  = vk::DescriptorType::eStorageBuffer,
              .pBufferInfo     = &info,
        };
        device.updateDescriptorSets(write, {});
        frame.hitBufferRevision = graph.bufferRevision(hitData);
    }
}

void OfflinePathTracingPass::updateTlasDescriptor(RenderGraph& graph, const PassFrame& execution, Frame& frame)
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
            .dstBinding      = OfflineRayTracingBinding::kTlas,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eAccelerationStructureKHR,
        };
        device.updateDescriptorSets(write, {});
        frame.tlasRevision = structure.revision();
    }
}

void OfflinePathTracingPass::writeDescriptorSet(RenderGraph& graph, const PassSetup& setup)
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
    const auto                     outputInfo   = imageInfo(rayImage);
    const auto&                    accumulation = graph.getBuffer(accumulationHandle);
    const vk::DescriptorBufferInfo accumulationInfo{.buffer = accumulation.handle(), .range = accumulation.size()};

    const auto&                    splats = graph.getBuffer(splatHandle);
    const vk::DescriptorBufferInfo splatInfo{.buffer = splats.handle(), .range = splats.size()};
    const auto&                    distribution = graph.getBuffer(lightDistributionHandle);
    const vk::DescriptorBufferInfo distributionInfo{.buffer = distribution.handle(), .range = distribution.size()};

    const std::array writes{
        vk::WriteDescriptorSet{
            .dstSet          = *frame.set,
            .dstBinding      = OfflineRayTracingBinding::kLightDistribution,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &distributionInfo,
        },
        vk::WriteDescriptorSet{
            .dstSet          = *frame.set,
            .dstBinding      = OfflineRayTracingBinding::kColor,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageImage,
            .pImageInfo      = &outputInfo,
        },
        vk::WriteDescriptorSet{
            .dstSet          = *frame.set,
            .dstBinding      = OfflineRayTracingBinding::kAccumulation,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &accumulationInfo,
        },
        vk::WriteDescriptorSet{
            .dstSet          = *frame.set,
            .dstBinding      = OfflineRayTracingBinding::kSplats,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &splatInfo,
        },
    };
    setup.vulkan.deviceHandle().updateDescriptorSets(writes, {});
}

void OfflinePathTracingPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution
)
{
    auto& commands = commandBuffer;
    DCHECK(execution.frameIndex == 0);
    CHECK(accumulatedSamples < std::numeric_limits<uint32_t>::max());
    updateTlasDescriptor(graph, execution, frame);
    commands.bindDescriptorSets(
        vk::PipelineBindPoint::eRayTracingKHR,
        pipelineLayout,
        0,
        std::array{execution.data.sceneSetHandle()},
        {}
    );
    commands.bindDescriptorSets(
        vk::PipelineBindPoint::eRayTracingKHR,
        pipelineLayout,
        2,
        std::array{execution.renderData.materialSetHandle(), *frame.set},
        {}
    );

    const auto& viewport = execution.view.viewport;

    const auto& offline  = *execution.renderData.offlineScene;
    const auto& settings = execution.setup.config.feature.offline;
    const bool  bdpt     = settings.algorithm == OfflineAlgorithm::BDPT;
    const auto& uniforms = execution.view.uniforms;

    const auto forward      = glm::normalize(-glm::vec3{uniforms.view[0][2], uniforms.view[1][2], uniforms.view[2][2]});
    const auto onImagePlane = [&](glm::vec2 ndc)
    {
        const auto homogeneous = uniforms.inverseViewProjection * glm::vec4{ndc, 1, 1};
        const auto offset      = glm::vec3{homogeneous} / homogeneous.w - glm::vec3{uniforms.cameraPosition};
        return offset / glm::dot(offset, forward);
    };
    const auto  corner = onImagePlane({-1, -1});
    const float imagePlaneArea =
        glm::length(glm::cross(onImagePlane({1, -1}) - corner, onImagePlane({-1, 1}) - corner));

    uint32_t flags = 0;
    if (settings.bdpt.lightTracing)
        flags |= kOfflineBdptLightTracing;
    if (settings.bdpt.misHeuristic == BdptMisHeuristic::Power)
        flags |= kOfflineBdptPowerHeuristic;

    const auto extent = graph.imageExtent(rayImage);
    const auto firstX = uint32_t(std::clamp(int32_t(std::ceil(viewport.x - 0.5f)), 0, int32_t(extent.width)));
    const auto firstY = uint32_t(std::clamp(int32_t(std::ceil(viewport.y - 0.5f)), 0, int32_t(extent.height)));
    const auto endX   = uint32_t(
        std::clamp(int32_t(std::ceil(viewport.x + viewport.width - 0.5f)), int32_t(firstX), int32_t(extent.width))
    );
    const auto endY = uint32_t(
        std::clamp(int32_t(std::ceil(viewport.y + viewport.height - 0.5f)), int32_t(firstY), int32_t(extent.height))
    );
    const uint64_t viewportPixels = uint64_t(endX - firstX) * (endY - firstY);

    glm::uvec2 tileOffset{0};
    glm::uvec2 batchExtent{extent.width, extent.height};
    uint32_t   tileCount    = 1;
    const bool trace        = activePipeline && viewportPixels > 0;
    float      lightSamples = accumulatedProgress;

    if (trace)
    {
        if (bdpt)
        {
            const auto depthScale =
                std::bit_ceil((settings.maxBounces() + kBdptTileReferenceDepth - 1) / kBdptTileReferenceDepth);
            const auto tileSize = std::max(8u, kBdptTileSize / depthScale);
            const auto columns  = (endX - firstX + tileSize - 1) / tileSize;
            const auto rows     = (endY - firstY + tileSize - 1) / tileSize;
            tileCount           = columns * rows;
            tileOffset          = {firstX + tileIndex % columns * tileSize, firstY + tileIndex / columns * tileSize};
            batchExtent         = {std::min(tileSize, endX - tileOffset.x), std::min(tileSize, endY - tileOffset.y)};
            lightSamples        = float(accumulatedSamples) +
                float(processedPixels + uint64_t(batchExtent.x) * batchExtent.y) / float(viewportPixels);
        }
        else
        {
            lightSamples = float(accumulatedSamples) + 1.0f;
        }
    }

    const OfflinePathParameters parameters{
        .viewport     = {viewport.x, viewport.y, viewport.width, viewport.height},
        .samples      = {accumulatedSamples, settings.maxBounces(), flags},
        .lightSamples = lightSamples,
        .lightCounts =
            {offline.pointLights.size(),
             offline.spotLights.size(),
             offline.directionalLights.size(),
             offline.lightTriangles.size()},
        .mappedLightCounts =
            {offline.hasEnvironmentLight ? 1u : 0u,
             offline.projectionLights.size(),
             offline.goniometricLights.size(),
             offline.lightPortals.size()},
        .sceneSphere = sceneSphere,
        .camera      = {forward, imagePlaneArea},
        .dispatch    = {tileOffset, extent.width, extent.height},
    };
    commands.pushConstants<OfflinePathParameters>(pipelineLayout, kParameterStages, 0, parameters);

    if (resetAccumulation)
    {
        const auto& accumulation = graph.getBuffer(accumulationHandle);
        commands.fillBuffer(accumulation.handle(), 0, accumulation.size(), 0);

        const auto& splats = graph.getBuffer(splatHandle);
        commands.fillBuffer(splats.handle(), 0, splats.size(), 0);

        const vk::MemoryBarrier2 clearBarrier{
            .srcStageMask  = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask =
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        };
        commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &clearBarrier});
        resetAccumulation = false;
    }

    if (trace)
    {
        commands.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *activePipeline->trace);
        commands.traceRaysKHR(
            activePipeline->raygen,
            activePipeline->miss,
            activePipeline->hit,
            {},
            batchExtent.x,
            batchExtent.y,
            1
        );

        const vk::MemoryBarrier2 resolveBarrier{
            .srcStageMask  = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .dstStageMask  = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
        };
        commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &resolveBarrier});

        accumulatedProgress = lightSamples;
        processedPixels += uint64_t(batchExtent.x) * batchExtent.y;
        if (++tileIndex == tileCount)
        {
            ++accumulatedSamples;
            tileIndex       = 0;
            processedPixels = 0;
        }
    }

    commands.bindPipeline(vk::PipelineBindPoint::eCompute, bdpt ? *bdptResolvePipeline : *ptResolvePipeline);
    commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 3, std::array{*frame.set}, {});
    commands.dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, 1);
}

void OfflinePathTracingPass::prepare(RenderGraph& graph, const PassFrame& execution)
{
    prepareHits(graph, execution, frame);

    const auto& offline        = *execution.renderData.offlineScene;
    const bool  offlineChanged = uploadedSceneRevision != offline.revision;
    if (offlineChanged)
    {
        const auto upload = [&](uint32_t index, const auto& values)
        {
            using Value      = typename std::remove_cvref_t<decltype(values)>::value_type;
            const auto bytes = std::max<size_t>(values.size(), 1) * sizeof(Value);
            CHECK(
                bytes <= execution.setup.vulkan.properties().limits.maxStorageBufferRange,
                "offline scene buffer {} exceeds maxStorageBufferRange: {} bytes",
                index,
                bytes
            );
            const auto allocation = graph.allocateUpload(sceneBuffers[index], bytes);
            if (values.empty())
                std::construct_at(reinterpret_cast<Value*>(allocation.data.data()));
            else
                std::memcpy(allocation.data.data(), values.data(), values.size() * sizeof(Value));
            if (sceneBufferRevisions[index] != graph.bufferRevision(sceneBuffers[index]))
            {
                const auto&                    buffer = graph.getBuffer(sceneBuffers[index]);
                const vk::DescriptorBufferInfo info{.buffer = buffer.handle(), .range = buffer.size()};
                const vk::WriteDescriptorSet   write{
                      .dstSet          = *frame.set,
                      .dstBinding      = kSceneBufferBindings[index],
                      .descriptorCount = 1,
                      .descriptorType  = vk::DescriptorType::eStorageBuffer,
                      .pBufferInfo     = &info
                };
                execution.setup.vulkan.deviceHandle().updateDescriptorSets(write, {});
                sceneBufferRevisions[index] = graph.bufferRevision(sceneBuffers[index]);
            }
        };

        upload(0, offline.materials);
        upload(1, offline.textures);
        upload(2, offline.areaEmissions);
        upload(3, offline.pointLights);
        upload(4, offline.spotLights);
        upload(5, offline.directionalLights);
        upload(6, offline.lightTriangles);
        upload(7, std::array{offline.environmentLight});
        upload(8, offline.projectionLights);
        upload(9, offline.goniometricLights);
        upload(10, offline.lightPortals);
        uploadedSceneRevision = offline.revision;
    }

    const auto& view          = execution.view;
    const auto& scene         = execution.renderData;
    const auto  assetRevision = context().assetManager->contentRevision();
    const bool  cameraChanged = view.uniforms.inverseViewProjection != previousView.uniforms.inverseViewProjection ||
        view.uniforms.cameraPosition != previousView.uniforms.cameraPosition ||
        view.viewport.x != previousView.viewport.x || view.viewport.y != previousView.viewport.y ||
        view.viewport.width != previousView.viewport.width || view.viewport.height != previousView.viewport.height;
    const bool  sceneChanged = !std::ranges::equal(previousDrawItems, scene.drawItems);
    const auto& settings     = execution.setup.config.feature.offline;

    if (!activePipeline || pipelineAlgorithm != settings.algorithm || pipelineDepth != shaderDepth(settings))
        preparePipeline(execution.setup);

    if (offlineChanged || sceneChanged || cameraChanged || previousAssetRevision != assetRevision ||
        previousSettings != settings)
    {
        accumulatedSamples  = 0;
        accumulatedProgress = 0;
        tileIndex           = 0;
        processedPixels     = 0;
        resetAccumulation   = true;
    }

    if (sceneChanged || cameraChanged || offlineChanged || previousAssetRevision != assetRevision)
    {
        if (scene.drawItems.empty())
        {
            sceneSphere = {glm::vec3{view.uniforms.cameraPosition}, kMinimumSceneRadius};
        }
        else
        {
            auto minimum = glm::vec3{std::numeric_limits<float>::max()};
            auto maximum = -minimum;
            for (const auto& item : scene.drawItems)
            {
                const auto bounds = context().assetManager->get(item.mesh).bounds.transformed(item.model);
                minimum           = glm::min(minimum, bounds.min);
                maximum           = glm::max(maximum, bounds.max);
            }
            const auto center = (minimum + maximum) * 0.5f;
            sceneSphere = {center, std::max(glm::length(maximum - center) * kSceneBoundsPadding, kMinimumSceneRadius)};
        }
    }

    if (settings.algorithm == OfflineAlgorithm::BDPT &&
        (offlineChanged || sceneChanged || cameraChanged || previousAssetRevision != assetRevision ||
         previousSettings.algorithm != settings.algorithm ||
         previousSettings.bdpt.lightSelection != settings.bdpt.lightSelection || lightDistributionRevision == 0))
        prepareLightDistribution(graph, execution);

    previousDrawItems.assign(scene.drawItems.begin(), scene.drawItems.end());
    previousView          = view;
    previousAssetRevision = assetRevision;
    previousSettings      = settings;
}

void OfflinePathTracingPass::prepareLightDistribution(RenderGraph& graph, const PassFrame& execution)
{
    const auto&      scene    = *execution.renderData.offlineScene;
    const bool       power    = execution.setup.config.feature.offline.bdpt.lightSelection == BdptLightSelection::Power;
    constexpr double kPi      = 3.14159265358979323846;
    const double     diskArea = kPi * double(sceneSphere.w) * sceneSphere.w;

    const auto intensity = [](const OfflineLightEmission& emission)
    {
        const double value = (emission.resources.w & kOfflineLightHasImage) != 0
            ? 1.0
            : std::max({emission.colorScale.r, emission.colorScale.g, emission.colorScale.b});
        return std::max(0.0, value * emission.colorScale.w);
    };

    std::vector<double> weights;
    const auto          append = [&](double weight) { weights.push_back(power ? weight : 1.0); };
    for (const auto& light : scene.pointLights)
        append(4.0 * kPi * intensity(light.emission));
    for (const auto& light : scene.spotLights)
        append(
            2.0 * kPi * ((1.0 - light.cosInner) + (light.cosInner - light.cosOuter) / 5.0) * intensity(light.emission)
        );
    for (const auto& light : scene.directionalLights)
        append(diskArea * intensity(light.emission));
    for (const auto& light : scene.lightTriangles)
        append(
            light.p0Area.w * kPi * (light.twoSided ? 2.0 : 1.0) * intensity(scene.areaEmissions[light.emissionIndex])
        );
    if (scene.hasEnvironmentLight)
        append(4.0 * kPi * diskArea * intensity(scene.environmentLight.emission));

    double sum = 0;
    for (const auto weight : weights)
        sum += weight;
    CHECK(std::isfinite(sum));

    const auto count = weights.size();
    const auto bytes = std::max<size_t>(count, 1) * sizeof(OfflineLightDistributionEntry);
    CHECK(bytes <= execution.setup.vulkan.properties().limits.maxStorageBufferRange);

    const auto upload  = graph.allocateUpload(lightDistributionHandle, bytes);
    auto*      entries = reinterpret_cast<OfflineLightDistributionEntry*>(upload.data.data());

    std::vector<uint32_t> small, large;
    small.reserve(count);
    large.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        const double probability = sum > 0.0 ? weights[index] / sum : 0.0;
        std::construct_at(
            entries + index,
            OfflineLightDistributionEntry{
                .alias       = index,
                .acceptance  = 1.0f,
                .probability = static_cast<float>(probability),
            }
        );
        weights[index] = probability * count;
        (weights[index] < 1.0 ? small : large).push_back(index);
    }

    while (!small.empty() && !large.empty())
    {
        const auto below = small.back();
        small.pop_back();
        const auto above = large.back();
        large.pop_back();

        entries[below].acceptance = static_cast<float>(weights[below]);
        entries[below].alias      = above;
        weights[above] += weights[below] - 1.0;
        (weights[above] < 1.0 ? small : large).push_back(above);
    }

    if (count == 0)
        std::construct_at(entries);

    if (lightDistributionRevision != graph.bufferRevision(lightDistributionHandle))
    {
        const auto&                    buffer = graph.getBuffer(lightDistributionHandle);
        const vk::DescriptorBufferInfo info{.buffer = buffer.handle(), .range = buffer.size()};
        const vk::WriteDescriptorSet   write{
              .dstSet          = *frame.set,
              .dstBinding      = OfflineRayTracingBinding::kLightDistribution,
              .descriptorCount = 1,
              .descriptorType  = vk::DescriptorType::eStorageBuffer,
              .pBufferInfo     = &info,
        };
        execution.setup.vulkan.deviceHandle().updateDescriptorSets(write, {});
        lightDistributionRevision = graph.bufferRevision(lightDistributionHandle);
    }
}
