#include "render/raytracing/offline/OfflinePathTracing.h"
#include "render/device/ResourceBarriers.h"

#include "asset/AssetManager.h"
#include "core/Context.h"
#include "core/Vertex.h"
#include "render/FrameData.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/device/VulkanContext.h"
#include "render/raytracing/AccelerationStructure.h"
#include "render/raytracing/RayTracingPipeline.h"
#include "render/raytracing/offline/OfflineScene.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr auto kParameterStages = vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute;

struct alignas(16) AccumulatedPixel
{
    glm::vec3 color;
    uint32_t  sampleCount;
};
static_assert(sizeof(AccumulatedPixel) == 16);

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

constexpr std::array<vk::DeviceSize, kSceneBufferBindings.size()> kSceneBufferSizes{
    sizeof(pbrt::MaterialInstruction),
    sizeof(pbrt::TextureInstruction),
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

}  // namespace

void OfflinePathTracing::compile(
    const Falcor::RenderContext&                    context,
    Falcor::uint2                                           frameDims,
    std::string_view                                resolveProgram,
    uint32_t                                        parameterSize,
    std::span<const vk::DescriptorSetLayoutBinding> extraBindings
)
{
    auto& cache = *context.vulkanResources;
    const auto& vulkan = *context.vulkan;
    CHECK(vulkan.supportsRayTracing(), "selected GPU does not support the ray tracing route");

    const auto&    device            = vulkan.physicalDeviceHandle();
    const auto     features          = device.getFormatProperties(kHdrColorFormat).optimalTilingFeatures;
    constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eStorageImage |
        vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    CHECK(
        (features & kRequiredFeatures) == kRequiredFeatures,
        "GPU does not support RGBA16F ray tracing output with storage writes and linear sampling"
    );

    auto& renderDevice = *context.getDevice();
    constexpr auto kStorage = Falcor::ResourceBindFlags::UnorderedAccess;
    extent = vk::Extent2D{frameDims.x, frameDims.y};
    hitBuffer = renderDevice.createBuffer(sizeof(OfflineHitData), kStorage);
    accumulationBuffer = renderDevice.createBuffer(size_t{extent.width} * extent.height * sizeof(AccumulatedPixel), kStorage);
    hitData = cache.registerBuffer(hitBuffer);
    accumulation = cache.registerBuffer(accumulationBuffer);

    accesses.clear();
    constexpr auto kRead = vulkan_access::storageRead(vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
    accesses.add(hitData, kRead);
    for (size_t index = 0; index < sceneBuffers.size(); ++index)
    {
        sceneBufferResources[index] = renderDevice.createBuffer(kSceneBufferSizes[index], kStorage);
        sceneBuffers[index] = cache.registerBuffer(sceneBufferResources[index]);
        accesses.add(sceneBuffers[index], kRead);
    }
    accesses.declare(cache);
    barriers.clear();

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
            {.binding         = OfflineRayTracingBinding::kAccumulation,
             .descriptorType  = vk::DescriptorType::eStorageBuffer,
             .descriptorCount = 1,
             .stageFlags      = kParameterStages},
            {.binding         = OfflineRayTracingBinding::kColor,
             .descriptorType  = vk::DescriptorType::eStorageImage,
             .descriptorCount = 1,
             .stageFlags      = vk::ShaderStageFlagBits::eCompute},
        };
        for (const auto binding : kSceneBufferBindings)
            bindings.push_back({
                .binding         = binding,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR,
            });
        bindings.insert(bindings.end(), extraBindings.begin(), extraBindings.end());

        descriptorSetLayout = renderDevice.createDescriptorSetLayout(bindings);
        descriptorSet       = renderDevice.allocateDescriptorSet(descriptorSetLayout);

        const vk::PushConstantRange parameterRange{
            .stageFlags = kParameterStages,
            .size       = parameterSize,
        };
        emptySetLayout = renderDevice.createDescriptorSetLayout({});
        const std::array descriptorSetLayouts{
            &renderDevice.sceneDescriptorSetLayout,
            &emptySetLayout,
            &renderDevice.materialDescriptorSetLayout,
            &descriptorSetLayout
        };
        pipelineLayout = renderDevice.getOrCreatePipelineLayout(descriptorSetLayouts, std::array{parameterRange});

        const auto shader = context.shaderManager->getOrLoad(resolveProgram);
        resolvePipeline   = renderDevice.createComputePipeline(shader, pipelineLayout);
    }

    hitBufferRevision = 0;
    tlasRevision      = 0;
    sceneBufferRevisions.fill(0);
    uploadedSceneRevision = 0;
    resetAccumulation();

    cache.writeDescriptor(*descriptorSet, OfflineRayTracingBinding::kAccumulation, accumulation);
}

void OfflinePathTracing::bindStorageBuffer(
    VulkanResources& cache, const VulkanContext& vulkan, VulkanResources::BufferHandle handle, uint32_t binding
) const
{
    const auto&                    buffer = cache.getBuffer(handle);
    const vk::DescriptorBufferInfo info{.buffer = buffer.handle(), .range = buffer.getSize()};
    const vk::WriteDescriptorSet   write{
          .dstSet          = *descriptorSet,
          .dstBinding      = binding,
          .descriptorCount = 1,
          .descriptorType  = vk::DescriptorType::eStorageBuffer,
          .pBufferInfo     = &info,
    };
    vulkan.deviceHandle().updateDescriptorSets(write, {});
}

void OfflinePathTracing::prepareHits(VulkanResources& cache, const Falcor::RenderContext& execution)
{
    const auto& device    = execution.vulkan->deviceHandle();
    auto&       resources = *execution.resources;
    static_assert(
        sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24 &&
        offsetof(Vertex, tangent) == 32
    );

    const auto& items  = execution.scene->drawItems;
    const auto  bytes  = std::max<size_t>(items.size(), 1) * sizeof(OfflineHitData);
    const auto  upload = cache.allocateUpload(hitData, bytes);
    auto*       hits   = reinterpret_cast<OfflineHitData*>(upload.data.data());
    for (uint32_t index = 0; index < items.size(); ++index)
    {
        const auto& item = items[index];
        const auto& mesh = resources.mesh(item.mesh);
        const auto& draw = execution.scene->offlineScene->draws[index];
        const auto& part = mesh.submeshes()[item.submeshIndex];
        std::construct_at(
            hits + index,
            OfflineHitData{
                .vertexAddress = mesh.vertices().getGpuAddress(),
                .indexAddress  = mesh.indices().getGpuAddress(),
                .material      = {part.firstIndex, draw.materialIndex, draw.reverseOrientation, draw.firstAreaLight},
            }
        );
    }
    if (items.empty())
        std::construct_at(hits, OfflineHitData{});

    const auto& buffer = cache.getBuffer(hitData);
    if (hitBufferRevision != cache.bufferRevision(hitData))
    {
        const vk::DescriptorBufferInfo info{.buffer = buffer.handle(), .range = buffer.getSize()};
        const vk::WriteDescriptorSet   write{
              .dstSet          = *descriptorSet,
              .dstBinding      = OfflineRayTracingBinding::kObjects,
              .descriptorCount = 1,
              .descriptorType  = vk::DescriptorType::eStorageBuffer,
              .pBufferInfo     = &info,
        };
        device.updateDescriptorSets(write, {});
        hitBufferRevision = cache.bufferRevision(hitData);
    }
}

void OfflinePathTracing::updateTlasDescriptor(const Falcor::RenderContext& execution)
{
    DCHECK(execution.sceneTlas);
    const auto& device    = execution.vulkan->deviceHandle();
    const auto& structure = *execution.sceneTlas;
    if (tlasRevision != structure.revision())
    {
        const auto                                           tlas = structure.handle();
        const vk::WriteDescriptorSetAccelerationStructureKHR accelerationInfo{
            .accelerationStructureCount = 1,
            .pAccelerationStructures    = &tlas,
        };
        const vk::WriteDescriptorSet write{
            .pNext           = &accelerationInfo,
            .dstSet          = *descriptorSet,
            .dstBinding      = OfflineRayTracingBinding::kTlas,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eAccelerationStructureKHR,
        };
        device.updateDescriptorSets(write, {});
        tlasRevision = structure.revision();
    }
}

bool OfflinePathTracing::prepareScene(
    VulkanResources& cache, const Falcor::RenderContext& execution, const Falcor::ref<Falcor::Texture>& color
)
{
    if (barriers.empty())
    {
        constexpr auto kRayTracing = vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        constexpr auto kCompute    = vk::PipelineStageFlagBits2::eComputeShader;
        constexpr auto kTransfer   = vk::PipelineStageFlagBits2::eTransfer;
        barriers.add(execution.materialTable->bufferHandle(), vulkan_access::storageRead(kRayTracing));
        const auto output = cache.registerImage(color);
        barriers.add(
            output,
            VulkanResources::ImageAccess{
                kCompute,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::ImageLayout::eGeneral,
            }
        );
        barriers.add(
            accumulation,
            VulkanResources::MemoryAccess{
                kTransfer | kRayTracing | kCompute,
                vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageRead |
                    vk::AccessFlagBits2::eShaderStorageWrite,
            }
        );
        barriers.declare(cache);
        cache.writeDescriptor(
            *descriptorSet,
            OfflineRayTracingBinding::kColor,
            output,
            vk::DescriptorType::eStorageImage
        );
    }

    barriers.transition(cache, *execution.commands);
    prepareHits(cache, execution);

    const auto& offline        = *execution.scene->offlineScene;
    const bool  offlineChanged = uploadedSceneRevision != offline.revision;
    if (offlineChanged)
    {
        const auto upload = [&](uint32_t index, const auto& values)
        {
            using Value      = typename std::remove_cvref_t<decltype(values)>::value_type;
            const auto bytes = std::max<size_t>(values.size(), 1) * sizeof(Value);
            CHECK(
                bytes <= execution.vulkan->properties().limits.maxStorageBufferRange,
                "offline scene buffer {} exceeds maxStorageBufferRange: {} bytes",
                index,
                bytes
            );
            const auto allocation = cache.allocateUpload(sceneBuffers[index], bytes);
            if (values.empty())
                std::construct_at(reinterpret_cast<Value*>(allocation.data.data()));
            else
                std::memcpy(allocation.data.data(), values.data(), values.size() * sizeof(Value));
            if (sceneBufferRevisions[index] != cache.bufferRevision(sceneBuffers[index]))
            {
                const auto&                    buffer = cache.getBuffer(sceneBuffers[index]);
                const vk::DescriptorBufferInfo info{.buffer = buffer.handle(), .range = buffer.getSize()};
                const vk::WriteDescriptorSet   write{
                      .dstSet          = *descriptorSet,
                      .dstBinding      = kSceneBufferBindings[index],
                      .descriptorCount = 1,
                      .descriptorType  = vk::DescriptorType::eStorageBuffer,
                      .pBufferInfo     = &info
                };
                execution.vulkan->deviceHandle().updateDescriptorSets(write, {});
                sceneBufferRevisions[index] = cache.bufferRevision(sceneBuffers[index]);
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

    const auto& view          = *execution.view;
    const auto& scene         = *execution.scene;
    const auto  assetRevision = context().assetManager->contentRevision();
    const bool  cameraChanged = view.uniforms.inverseViewProjection != previousView.uniforms.inverseViewProjection ||
        view.uniforms.cameraPosition != previousView.uniforms.cameraPosition ||
        view.viewport.x != previousView.viewport.x || view.viewport.y != previousView.viewport.y ||
        view.viewport.width != previousView.viewport.width || view.viewport.height != previousView.viewport.height;
    const bool sceneChanged = !std::ranges::equal(previousDrawItems, scene.drawItems);
    const bool assetChanged = previousAssetRevision != assetRevision;

    if (sceneChanged)
        previousDrawItems.assign(scene.drawItems.begin(), scene.drawItems.end());
    previousView          = view;
    previousAssetRevision = assetRevision;
    return offlineChanged || sceneChanged || cameraChanged || assetChanged;
}

vk::Rect2D OfflinePathTracing::viewportBounds(const RenderView& view) const
{
    const auto& viewport = view.viewport;
    const auto  firstX   = std::clamp(int32_t(std::ceil(viewport.x - 0.5f)), 0, int32_t(extent.width));
    const auto  firstY   = std::clamp(int32_t(std::ceil(viewport.y - 0.5f)), 0, int32_t(extent.height));
    const auto endX = std::clamp(int32_t(std::ceil(viewport.x + viewport.width - 0.5f)), firstX, int32_t(extent.width));
    const auto endY =
        std::clamp(int32_t(std::ceil(viewport.y + viewport.height - 0.5f)), firstY, int32_t(extent.height));
    return {{firstX, firstY}, {uint32_t(endX - firstX), uint32_t(endY - firstY)}};
}

OfflinePathParameters OfflinePathTracing::pathParameters(const Falcor::RenderContext& execution, uint32_t maxBounces) const
{
    const auto& viewport = execution.view->viewport;
    const auto& scene    = *execution.scene->offlineScene;
    return {
        .viewport  = {viewport.x, viewport.y, viewport.width, viewport.height},
        .samples   = {completedSamples, maxBounces},
        .imageSize = {extent.width, extent.height},
        .lightCounts =
            {scene.pointLights.size(),
             scene.spotLights.size(),
             scene.directionalLights.size(),
             scene.lightTriangles.size()},
        .mappedLightCounts = {
            scene.hasEnvironmentLight ? 1u : 0u,
            scene.projectionLights.size(),
            scene.goniometricLights.size(),
            scene.lightPortals.size()
        },
    };
}

void OfflinePathTracing::resetAccumulation()
{
    completedSamples = 0;
    clearPending     = true;
}

void OfflinePathTracing::render(
    VulkanResources&                               cache,
    vk::raii::CommandBuffer&                       commands,
    const Falcor::RenderContext&                           execution,
    std::span<const std::byte>                     constants,
    const RayTracingPipeline*                      pipeline,
    vk::Extent2D                                   rayExtent,
    std::span<const VulkanResources::BufferHandle> extraAccumulations
)
{
    accesses.transition(cache, commands);

    DCHECK(execution.frameIndex == 0);
    CHECK(completedSamples < std::numeric_limits<uint32_t>::max());
    commands.pushConstants<std::byte>(pipelineLayout, kParameterStages, 0, constants);

    if (clearPending)
    {
        const auto clear = [&](VulkanResources::BufferHandle handle)
        {
            const auto& buffer = cache.getBuffer(handle);
            commands.fillBuffer(buffer.handle(), 0, buffer.getSize(), 0);
        };
        clear(accumulation);
        for (const auto buffer : extraAccumulations)
            clear(buffer);

        const vk::MemoryBarrier2 clearBarrier{
            .srcStageMask  = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask =
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        };
        commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &clearBarrier});
        clearPending = false;
    }

    if (pipeline)
    {
        updateTlasDescriptor(execution);
        commands.bindDescriptorSets(
            vk::PipelineBindPoint::eRayTracingKHR,
            pipelineLayout,
            0,
            std::array{execution.frameData->sceneSetHandle()},
            {}
        );
        commands.bindDescriptorSets(
            vk::PipelineBindPoint::eRayTracingKHR,
            pipelineLayout,
            2,
            std::array{execution.scene->materialSetHandle(), *descriptorSet},
            {}
        );
        pipeline->bind(commands);
        pipeline->dispatch(commands, 0, {rayExtent.width, rayExtent.height, 1});

        const vk::MemoryBarrier2 resolveBarrier{
            .srcStageMask  = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .dstStageMask  = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
        };
        commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &resolveBarrier});
    }

    commands.bindPipeline(vk::PipelineBindPoint::eCompute, *resolvePipeline);
    commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 3, std::array{*descriptorSet}, {});
    resolvePipeline.dispatch(commands, {extent.width, extent.height, 1});
}
