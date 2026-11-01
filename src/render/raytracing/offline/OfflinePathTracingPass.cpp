#include "render/raytracing/offline/OfflinePathTracingPass.h"

#include "asset/AssetManager.h"
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
#include <cstring>
#include <limits>

namespace
{
constexpr uint32_t kMissShaderCount = 2;
constexpr uint32_t kHitShaderCount  = 1;
constexpr std::array kSceneBufferBindings{
    OfflineRayTracingBinding::kMaterials, OfflineRayTracingBinding::kTextures, OfflineRayTracingBinding::kAreaEmissions,
    OfflineRayTracingBinding::kPoints, OfflineRayTracingBinding::kSpots, OfflineRayTracingBinding::kDirectionals,
    OfflineRayTracingBinding::kTriangles, OfflineRayTracingBinding::kEnvironments, OfflineRayTracingBinding::kProjections,
    OfflineRayTracingBinding::kGoniometrics, OfflineRayTracingBinding::kPortals,
};
constexpr std::array kSceneBufferSizes{
    sizeof(OfflineMaterialPreview), sizeof(OfflineTextureInstruction), sizeof(OfflineLightEmission),
    sizeof(OfflinePointLight), sizeof(OfflineSpotLight), sizeof(OfflineDirectionalLight), sizeof(OfflineLightTriangle),
    sizeof(OfflineEnvironmentLight), sizeof(OfflineProjectionLight), sizeof(OfflineGoniometricLight), sizeof(glm::vec4),
};
constexpr auto kSceneBufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
vk::DeviceSize     alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

OfflinePathTracingPass::Output OfflinePathTracingPass::setup(
    RenderGraph& graph, const PassSetup& setup, TlasBuildPass::Output scene
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
    hitData   = graph.createBuffer({
          .size  = sizeof(OfflineHitData),
          .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    for (size_t index = 0; index < sceneBuffers.size(); ++index)
        sceneBuffers[index] = graph.createBuffer({.size = kSceneBufferSizes[index], .usage = kSceneBufferUsage});
    sceneBufferRevisions.fill(0);
    uploadedSceneRevision = 0;
    graph.use(hitData, {.stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                       .access = vk::AccessFlagBits2::eShaderStorageRead});
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
    accumulationHandle = graph.createBuffer({.size = accumulationBytes});
    accumulationHandle = graph.write(accumulationHandle);
    graph.use(
        accumulationHandle,
        {.stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
         .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite}
    );

    if (!*pipeline)
        createPipeline(setup);
    frame.hitBufferRevision = 0;
    frame.tlasRevision      = 0;

    accumulatedSamples = 0;
    return {rayImage};
}

void OfflinePathTracingPass::createPipeline(const PassSetup& setup)
{
    const auto& vulkan = setup.vulkan;
    const auto& device = vulkan.deviceHandle();
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

    if (!pipelineLayout)
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {.binding = OfflineRayTracingBinding::kTlas,
             .descriptorType = vk::DescriptorType::eAccelerationStructureKHR, .descriptorCount = 1,
             .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR},
            {.binding = OfflineRayTracingBinding::kObjects,
             .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1,
             .stageFlags = vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eRaygenKHR},
        };
        for (const auto binding : kSceneBufferBindings)
            bindings.push_back({.binding = binding, .descriptorType = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR});
        bindings.push_back({.binding = OfflineRayTracingBinding::kAccumulation,
            .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR});
        bindings.push_back({.binding = OfflineRayTracingBinding::kColor,
            .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR});
        auto& renderDevice  = *renderContext().device;
        descriptorSetLayout = renderDevice.createDescriptorSetLayout(bindings);
        frame.set           = renderDevice.allocateDescriptorSet(descriptorSetLayout);

        const vk::PushConstantRange parameterRange{
            .stageFlags = vk::ShaderStageFlagBits::eRaygenKHR,
            .size       = sizeof(OfflinePathParameters),
        };
        const std::array descriptorSetLayouts{
            &renderDevice.sceneDescriptorSetLayout,
            &renderDevice.lightDescriptorSetLayout,
            &renderDevice.materialDescriptorSetLayout,
            &descriptorSetLayout
        };
        pipelineLayout = renderDevice.getOrCreatePipelineLayout(descriptorSetLayouts, std::array{parameterRange});
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
}

void OfflinePathTracingPass::prepareHits(RenderGraph& graph, const PassFrame& execution, Frame& frame)
{
    const auto& device    = execution.setup.vulkan.deviceHandle();
    auto&       resources = *renderContext().resources;
    static_assert(sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24);
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
                .indexAddress = mesh.indices().address(device),
                .material = {part.firstIndex, draw.materialIndex, draw.reverseOrientation, draw.firstAreaLight},
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
    const std::array               writes{
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
    commands.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
    const std::array sets{
        execution.data.sceneSetHandle(),
        execution.data.lightSetHandle(),
        execution.renderData.materialSetHandle(),
        *frame.set
    };
    commands.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, pipelineLayout, 0, sets, {});
    const auto& viewport = execution.view.viewport;
    const auto& offline = *execution.renderData.offlineScene;
    commands.pushConstants<OfflinePathParameters>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eRaygenKHR,
        0,
        OfflinePathParameters{
            .viewport           = {viewport.x, viewport.y, viewport.width, viewport.height},
            .samples = {accumulatedSamples, execution.setup.config.feature.offline.maxBounces, 0, 0},
            .lightCounts = {offline.pointLights.size(), offline.spotLights.size(),
                offline.directionalLights.size(), offline.lightTriangles.size()},
            .mappedLightCounts = {offline.hasEnvironmentLight ? 1u : 0u, offline.projectionLights.size(),
                offline.goniometricLights.size(), offline.lightPortals.size()},
        }
    );
    const auto extent = graph.imageExtent(rayImage);
    commands.traceRaysKHR(raygenRegion, missRegion, hitRegion, {}, extent.width, extent.height, 1);
    ++accumulatedSamples;
}

void OfflinePathTracingPass::prepare(RenderGraph& graph, const PassFrame& execution)
{
    prepareHits(graph, execution, frame);
    const auto& offline = *execution.renderData.offlineScene;
    const bool offlineChanged = uploadedSceneRevision != offline.revision;
    if (offlineChanged)
    {
        const auto upload = [&](uint32_t index, const auto& values)
        {
            using Value = typename std::remove_cvref_t<decltype(values)>::value_type;
            const auto bytes = std::max<size_t>(values.size(), 1) * sizeof(Value);
            CHECK(bytes <= execution.setup.vulkan.properties().limits.maxStorageBufferRange,
                  "offline scene buffer {} exceeds maxStorageBufferRange: {} bytes", index, bytes);
            const auto allocation = graph.allocateUpload(sceneBuffers[index], bytes);
            if (values.empty()) std::construct_at(reinterpret_cast<Value*>(allocation.data.data()));
            else std::memcpy(allocation.data.data(), values.data(), values.size() * sizeof(Value));
            if (sceneBufferRevisions[index] != graph.bufferRevision(sceneBuffers[index]))
            {
                const auto& buffer = graph.getBuffer(sceneBuffers[index]);
                const vk::DescriptorBufferInfo info{.buffer = buffer.handle(), .range = buffer.size()};
                const vk::WriteDescriptorSet write{.dstSet = *frame.set, .dstBinding = kSceneBufferBindings[index],
                    .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &info};
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
    const bool sceneChanged = !std::ranges::equal(previousDrawItems, scene.drawItems);
    const auto maxBounces = execution.setup.config.feature.offline.maxBounces;
    if (offlineChanged || sceneChanged || cameraChanged || previousAssetRevision != assetRevision ||
        previousMaxBounces != maxBounces)
        accumulatedSamples = 0;

    previousDrawItems.assign(scene.drawItems.begin(), scene.drawItems.end());
    previousView           = view;
    previousAssetRevision  = assetRevision;
    previousMaxBounces     = maxBounces;
}
