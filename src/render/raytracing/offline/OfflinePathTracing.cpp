#include "render/raytracing/offline/OfflinePathTracing.h"

#include "asset/AssetManager.h"
#include "core/Context.h"
#include "core/Vertex.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/raytracing/AccelerationStructure.h"
#include "render/raytracing/offline/OfflineScene.h"
#include "render/raytracing/offline/OfflineTracePipeline.h"
#include "render/resources/GpuShader.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr auto kParameterStages = vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute;
constexpr uint32_t kResolveGroupSize = 8;

struct alignas(16) AccumulatedPixel
{
    glm::vec3 color;
    uint32_t sampleCount;
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

constexpr std::array kSceneBufferSizes{
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

constexpr auto kSceneBufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;

}  // namespace

RenderGraph::ImageHandle OfflinePathTracing::compile(
    RenderGraph& graph, const PassSetup& setup, std::string_view resolveShader, uint32_t parameterSize,
    std::span<const vk::DescriptorSetLayoutBinding> extraBindings
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

    hitData = graph.createBuffer({
          .size  = sizeof(OfflineHitData),
          .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });

    for (size_t index = 0; index < sceneBuffers.size(); ++index)
        sceneBuffers[index] = graph.createBuffer({.size = kSceneBufferSizes[index], .usage = kSceneBufferUsage});

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

    extent   = setup.renderExtent;
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
        vk::DeviceSize(extent.width) * extent.height * sizeof(AccumulatedPixel);
    CHECK(
        accumulationBytes <= setup.vulkan.properties().limits.maxStorageBufferRange,
        "offline accumulation buffer exceeds maxStorageBufferRange: {} bytes",
        accumulationBytes
    );

    accumulation = graph.createBuffer(
        {.size  = accumulationBytes,
         .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst}
    );
    accumulation = graph.write(accumulation);
    graph.use(
        accumulation,
        {.stages = vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eRayTracingShaderKHR |
             vk::PipelineStageFlagBits2::eComputeShader,
         .access = vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageRead |
             vk::AccessFlagBits2::eShaderStorageWrite}
    );

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

        auto& renderDevice  = *renderContext().device;
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

        const auto& vkDevice = setup.vulkan.deviceHandle();
        GpuShader shader(vkDevice, std::string{resolveShader});
        resolvePipeline = vkCheck(vkDevice.createComputePipeline(
            nullptr,
            {
                .stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = shader.handle(), .pName = "computeMain"},
                .layout = pipelineLayout,
            }
        ));
    }

    hitBufferRevision = 0;
    tlasRevision = 0;
    sceneBufferRevisions.fill(0);
    uploadedSceneRevision = 0;
    resetAccumulation();
    return rayImage;
}

void OfflinePathTracing::bindStorageBuffer(
    RenderGraph& graph, const VulkanContext& vulkan, RenderGraph::BufferHandle handle, uint32_t binding
) const
{
    const auto& buffer = graph.getBuffer(handle);
    const vk::DescriptorBufferInfo info{.buffer = buffer.handle(), .range = buffer.size()};
    const vk::WriteDescriptorSet write{
        .dstSet          = *descriptorSet,
        .dstBinding      = binding,
        .descriptorCount = 1,
        .descriptorType  = vk::DescriptorType::eStorageBuffer,
        .pBufferInfo     = &info,
    };
    vulkan.deviceHandle().updateDescriptorSets(write, {});
}

void OfflinePathTracing::bindResources(RenderGraph& graph, const PassSetup& setup)
{
    bindStorageBuffer(graph, setup.vulkan, accumulation, OfflineRayTracingBinding::kAccumulation);
    const vk::DescriptorImageInfo info{
        .imageView = graph.getImage(rayImage).getOrCreateImageView({
            .viewType = vk::ImageViewType::e2D,
            .format = graph.imageInfo(rayImage).format,
            .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eGeneral,
    };
    const vk::WriteDescriptorSet write{
        .dstSet          = *descriptorSet,
        .dstBinding      = OfflineRayTracingBinding::kColor,
        .descriptorCount = 1,
        .descriptorType  = vk::DescriptorType::eStorageImage,
        .pImageInfo      = &info,
    };
    setup.vulkan.deviceHandle().updateDescriptorSets(write, {});
}

void OfflinePathTracing::prepareHits(RenderGraph& graph, const PassFrame& execution)
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
    if (hitBufferRevision != graph.bufferRevision(hitData))
    {
        const vk::DescriptorBufferInfo info{.buffer = buffer.handle(), .range = buffer.size()};
        const vk::WriteDescriptorSet   write{
              .dstSet          = *descriptorSet,
              .dstBinding      = OfflineRayTracingBinding::kObjects,
              .descriptorCount = 1,
              .descriptorType  = vk::DescriptorType::eStorageBuffer,
              .pBufferInfo     = &info,
        };
        device.updateDescriptorSets(write, {});
        hitBufferRevision = graph.bufferRevision(hitData);
    }
}

void OfflinePathTracing::updateTlasDescriptor(const PassFrame& execution)
{
    DCHECK(execution.sceneTlas);
    const auto& device    = execution.setup.vulkan.deviceHandle();
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

bool OfflinePathTracing::prepareScene(RenderGraph& graph, const PassFrame& execution)
{
    prepareHits(graph, execution);

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
                      .dstSet          = *descriptorSet,
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
    const bool assetChanged = previousAssetRevision != assetRevision;

    if (sceneChanged)
        previousDrawItems.assign(scene.drawItems.begin(), scene.drawItems.end());
    previousView = view;
    previousAssetRevision = assetRevision;
    return offlineChanged || sceneChanged || cameraChanged || assetChanged;
}

vk::Rect2D OfflinePathTracing::viewportBounds(const RenderView& view) const
{
    const auto& viewport = view.viewport;
    const auto firstX = std::clamp(int32_t(std::ceil(viewport.x - 0.5f)), 0, int32_t(extent.width));
    const auto firstY = std::clamp(int32_t(std::ceil(viewport.y - 0.5f)), 0, int32_t(extent.height));
    const auto endX = std::clamp(
        int32_t(std::ceil(viewport.x + viewport.width - 0.5f)), firstX, int32_t(extent.width)
    );
    const auto endY = std::clamp(
        int32_t(std::ceil(viewport.y + viewport.height - 0.5f)), firstY, int32_t(extent.height)
    );
    return {{firstX, firstY}, {uint32_t(endX - firstX), uint32_t(endY - firstY)}};
}

OfflinePathParameters OfflinePathTracing::pathParameters(const PassFrame& execution, uint32_t maxBounces) const
{
    const auto& viewport = execution.view.viewport;
    const auto& scene = *execution.renderData.offlineScene;
    return {
        .viewport = {viewport.x, viewport.y, viewport.width, viewport.height},
        .samples = {completedSamples, maxBounces},
        .imageSize = {extent.width, extent.height},
        .lightCounts = {
            scene.pointLights.size(), scene.spotLights.size(), scene.directionalLights.size(), scene.lightTriangles.size()
        },
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
    clearPending = true;
}

void OfflinePathTracing::render(
    RenderGraph& graph,
    vk::raii::CommandBuffer& commands,
    const PassFrame& execution,
    std::span<const std::byte> constants,
    const OfflineTracePipeline* pipeline,
    vk::Extent2D rayExtent,
    std::span<const RenderGraph::BufferHandle> extraAccumulations
)
{
    DCHECK(execution.frameIndex == 0);
    CHECK(completedSamples < std::numeric_limits<uint32_t>::max());
    commands.pushConstants<std::byte>(pipelineLayout, kParameterStages, 0, constants);

    if (clearPending)
    {
        const auto clear = [&](RenderGraph::BufferHandle handle)
        {
            const auto& buffer = graph.getBuffer(handle);
            commands.fillBuffer(buffer.handle(), 0, buffer.size(), 0);
        };
        clear(accumulation);
        for (const auto buffer : extraAccumulations)
            clear(buffer);

        const vk::MemoryBarrier2 clearBarrier{
            .srcStageMask  = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask  = vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        };
        commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &clearBarrier});
        clearPending = false;
    }

    if (pipeline)
    {
        updateTlasDescriptor(execution);
        commands.bindDescriptorSets(
            vk::PipelineBindPoint::eRayTracingKHR, pipelineLayout, 0, std::array{execution.data.sceneSetHandle()}, {}
        );
        commands.bindDescriptorSets(
            vk::PipelineBindPoint::eRayTracingKHR, pipelineLayout, 2,
            std::array{execution.renderData.materialSetHandle(), *descriptorSet}, {}
        );
        commands.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline->trace);
        commands.traceRaysKHR(
            pipeline->raygen, pipeline->miss, pipeline->hit, {}, rayExtent.width, rayExtent.height, 1
        );

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
    commands.dispatch(
        (extent.width + kResolveGroupSize - 1) / kResolveGroupSize, (extent.height + kResolveGroupSize - 1) / kResolveGroupSize,
        1
    );
}
