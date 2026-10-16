#include "render/raytracing/RayTracingPass.h"

#include "asset/BuiltinAssets.h"
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
#include "render/resource/GpuMaterial.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuShader.h"
#include "render/resource/RenderResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/geometric.hpp>

namespace
{
constexpr uint32_t kMissShaderCount = 2;
constexpr uint32_t kHitShaderCount  = 2;
// Match raytracing.slang. The surface hit group is record 0, the emitter hit group is record 1.
constexpr uint32_t kSceneMask      = 0x01;
constexpr uint32_t kLightMask      = 0x02;
constexpr uint32_t kLightHitRecord = 1;

vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

VkTransformMatrixKHR instanceTransform(const glm::mat4& model)
{
    VkTransformMatrixKHR transform{};
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            transform.matrix[row][column] = model[column][row];
    return transform;
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

RayTracingPass::Output RayTracingPass::setup(RenderGraph& graph, const PassSetup& setup)
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
    const auto maxRecursionDepth = context().config->rendererConfig().feature.rayTracing.maxRecursionDepth;
    if (!*pipeline || pipelineRecursionDepth != maxRecursionDepth)
        createPipeline(setup, maxRecursionDepth);
    return {rayImage};
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
            vk::DescriptorPoolSize{
                .type            = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = maxFramesInFlight * 3
            },
            vk::DescriptorPoolSize{
                .type            = vk::DescriptorType::eSampledImage,
                .descriptorCount = maxFramesInFlight * kMaxRayTracingTextures
            },
            vk::DescriptorPoolSize{
                .type            = vk::DescriptorType::eSampler,
                .descriptorCount = maxFramesInFlight * kMaxRayTracingSamplers
            },
        };
        descriptorPool = vkCheck(device.createDescriptorPool(
            {.flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
             .maxSets       = maxFramesInFlight,
             .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
             .pPoolSizes    = poolSizes.data()}
        ));
        for (auto& frame : frames)
            frame.set = std::move(vkCheck(device.allocateDescriptorSets(
                                              {.descriptorPool     = *descriptorPool,
                                               .descriptorSetCount = 1,
                                               .pSetLayouts        = &*descriptorLayout}
                                          ))
                                      .front());

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

void RayTracingPass::createRectLightGeometry(CommandRecorder& commandBuffer, const PassFrame& execution)
{
    const auto& vulkan = execution.setup.vulkan;
    const auto& device = vulkan.deviceHandle();
    // A unit XY rectangle facing local -Z. Every light shares this BLAS.
    constexpr std::array<float, 12>
        kVertices{-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f};
    constexpr std::array<uint32_t, 6> kIndices{0, 2, 1, 0, 3, 2};
    const auto                        usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
        vk::BufferUsageFlagBits::eShaderDeviceAddress;
    const auto host   = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    rectLightVertices = Buffer(vulkan.physicalDeviceHandle(), device, sizeof(kVertices), usage, host);
    rectLightIndices  = Buffer(vulkan.physicalDeviceHandle(), device, sizeof(kIndices), usage, host);
    rectLightVertices.upload(kVertices.data(), sizeof(kVertices));
    rectLightIndices.upload(kIndices.data(), sizeof(kIndices));

    VkAccelerationStructureGeometryKHR triangles{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR,
    };
    triangles.geometry.triangles = {
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData   = {.deviceAddress = rectLightVertices.address(device)},
        .vertexStride = sizeof(float) * 3,
        .maxVertex    = static_cast<uint32_t>(kVertices.size() / 3 - 1),
        .indexType    = VK_INDEX_TYPE_UINT32,
        .indexData    = {.deviceAddress = rectLightIndices.address(device)},
    };
    constexpr uint32_t kTriangleCount = kIndices.size() / 3;
    rectLightBlas.create(vulkan, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, triangles, kTriangleCount);
    rectLightBlas.build(vulkan, commandBuffer, triangles, kTriangleCount);
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

void RayTracingPass::prepareScene(
    RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution, Frame& frame
)
{
    const auto& vulkan   = execution.setup.vulkan;
    const auto& device   = vulkan.deviceHandle();
    auto&       commands = commandBuffer;
    const auto& items    = execution.renderData.drawItems;
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
    std::vector<RayTracingHitData>                  hits;
    std::vector<RayTracingMaterialData>             materials;
    std::map<const GpuMaterial*, uint32_t>          materialIndices;
    std::vector<vk::ImageView>                      textureViews;
    std::vector<vk::Sampler>                        samplerHandles;
    const auto white   = renderContext().resources->texture(BuiltinAssets::Texture::white);
    const auto sampler = renderContext().resources->sampler(BuiltinAssets::Sampler::linearRepeat);
    std::array<vk::DescriptorImageInfo, kMaxRayTracingTextures> images;
    std::array<vk::DescriptorImageInfo, kMaxRayTracingSamplers> samplers;
    images.fill({.imageView = white->imageView(), .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal});
    samplers.fill({.sampler = sampler});

    const auto addDescriptor = []<class T, size_t Capacity>(
                                   std::vector<T>&                                handles,
                                   std::array<vk::DescriptorImageInfo, Capacity>& descriptors,
                                   T                                              handle,
                                   vk::DescriptorImageInfo                        descriptor,
                                   std::string_view                               kind
                               )
    {
        const auto found = std::ranges::find(handles, handle);
        if (found != handles.end())
            return static_cast<uint32_t>(found - handles.begin());

        CHECK(handles.size() < Capacity, "ray tracing {} descriptors exceed capacity {}", kind, Capacity);
        const auto index = static_cast<uint32_t>(handles.size());
        handles.push_back(handle);
        descriptors[index] = descriptor;
        return index;
    };
    const auto bindTexture = [&](const GpuMaterial::Texture& texture)
    {
        DCHECK(texture.image && texture.sampler);
        const auto view = texture.image->imageView();
        return RayTracingTextureRef{
            .textureIndex = addDescriptor(
                textureViews,
                images,
                view,
                {.imageView = view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal},
                "texture"
            ),
            .samplerIndex =
                addDescriptor(samplerHandles, samplers, texture.sampler, {.sampler = texture.sampler}, "sampler"),
        };
    };
    const auto addMaterial = [&](const GpuMaterial& material)
    {
        const auto [found, added] = materialIndices.try_emplace(&material, static_cast<uint32_t>(materials.size()));
        if (added)
        {
            const auto& parameters = material.parameters();
            const auto& textures   = material.textureBindings();
            materials.push_back({
                .baseColorFactor   = parameters.baseColorFactor,
                .emissiveFactor    = parameters.emissiveFactor,
                .metallic          = parameters.metallic,
                .roughness         = parameters.roughness,
                .normalScale       = parameters.normalScale,
                .occlusionStrength = parameters.occlusionStrength,
                .alphaCutoff       = parameters.alphaCutoff,
                .alphaMode         = parameters.alphaMode,
                .doubleSided       = material.doubleSided() ? 1u : 0u,
                .baseColorTexture  = bindTexture(textures.baseColor),
                .normalTexture     = bindTexture(textures.normal),
                .metallicTexture   = bindTexture(textures.metallic),
                .roughnessTexture  = bindTexture(textures.roughness),
                .emissiveTexture   = bindTexture(textures.emissive),
                .occlusionTexture  = bindTexture(textures.occlusion),
            });
        }
        return found->second;
    };
    const auto append = [&](const GpuMesh&   mesh,
                            uint32_t         firstIndex,
                            uint32_t         indexCount,
                            const glm::mat4& model,
                            uint32_t         materialIndex)
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
        instance.transform                      = instanceTransform(model);
        instance.instanceCustomIndex            = static_cast<uint32_t>(hits.size());
        instance.mask                           = kSceneMask;
        instance.flags                          = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = bottom->second.address();
        instances.push_back(instance);
        hits.push_back({vertexAddress, indexAddress, firstIndex, materialIndex});
    };
    for (const auto& item : items)
        append(*item.mesh, item.firstIndex, item.indexCount, item.model, addMaterial(*item.material));

    const auto& lights = execution.renderData.lights;
    CHECK(lights.size() < (1u << 24), "ray tracing light index exceeds the custom-index limit");
    const auto& ranges = execution.renderData.lightRanges;
    for (uint32_t index = ranges.pointEnd; index < ranges.rectAreaEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.enable())
            continue;

        const glm::vec2 size{light.areaSizeCone};
        CHECK(
            std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 && size.y > 0,
            "rectangular light {} requires finite positive dimensions",
            index
        );
        if (!rectLightBlas.handle())
            createRectLightGeometry(commandBuffer, execution);

        const glm::vec3 right = light.areaRight();
        const glm::vec3 direction{light.direction};
        const glm::vec3 up = glm::cross(right, direction);
        const glm::mat4 model{
            glm::vec4{right * size.x, 0},
            glm::vec4{up * size.y, 0},
            glm::vec4{-direction, 0},
            glm::vec4{light.position(), 1}
        };
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform                              = instanceTransform(model);
        instance.instanceCustomIndex                    = index;
        instance.mask                                   = kLightMask;
        instance.instanceShaderBindingTableRecordOffset = kLightHitRecord;
        // Unlike scene meshes, light instances keep facing culling enabled for one-sided emission.
        instance.accelerationStructureReference = rectLightBlas.address();
        instances.push_back(instance);
    }
    buildBarrier(commands, false);

    const uint32_t count = static_cast<uint32_t>(instances.size());
    CHECK(
        std::max(count, 1u) * sizeof(RayTracingHitData) <= vulkan.properties().limits.maxStorageBufferRange,
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
            std::max(count, 1u) * sizeof(RayTracingHitData),
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
    }
    if (!hits.empty())
    {
        frame.hitData.upload(hits.data(), hits.size() * sizeof(hits.front()));
    }
    const vk::DeviceSize materialBytes = std::max<size_t>(materials.size(), 1) * sizeof(RayTracingMaterialData);
    CHECK(
        materialBytes <= vulkan.properties().limits.maxStorageBufferRange,
        "ray tracing material data exceeds maxStorageBufferRange"
    );
    if (materialBytes > frame.materialData.size())
        frame.materialData = Buffer(
            vulkan.physicalDeviceHandle(),
            device,
            materialBytes,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
    if (materials.empty())
    {
        const RayTracingMaterialData empty{};
        frame.materialData.upload(&empty, sizeof(empty));
    }
    else
        frame.materialData.upload(materials.data(), materials.size() * sizeof(RayTracingMaterialData));

    frame.tlas.build(vulkan, commands, top, count);
    buildBarrier(commands, true);

    const vk::AccelerationStructureKHR                   tlas = frame.tlas.handle();
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
    const vk::DescriptorBufferInfo materialInfo{
        .buffer = frame.materialData.handle(),
        .range  = frame.materialData.size()
    };
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
            .dstBinding      = 3,
            .descriptorCount = kMaxRayTracingTextures,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .pImageInfo      = images.data()
        },
        vk::WriteDescriptorSet{
            .dstSet          = *frame.set,
            .dstBinding      = 4,
            .descriptorCount = kMaxRayTracingSamplers,
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
        vk::WriteDescriptorSet{
            .dstSet          = *frame.set,
            .dstBinding      = 6,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &materialInfo
        },
    };
    device.updateDescriptorSets(writes, {});
}

void RayTracingPass::execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution)
{
    auto&  commands = commandBuffer;
    Frame& frame    = frames[execution.frameIndex];
    prepareLights(execution, frame);
    prepareScene(graph, commandBuffer, execution, frame);
    commands.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
    const std::array sets = {*frame.set};
    commands.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *pipelineLayout, 0, sets, {});
    const auto& view     = execution.data.viewUniforms();
    const auto& viewport = execution.view.viewport;
    commands.pushConstants<RayTracingParameters>(
        *pipelineLayout,
        vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR,
        0,
        RayTracingParameters{
            .inverseViewProjection = view.inverseViewProjection,
            .cameraPosition        = view.cameraPosition,
            .viewport              = {viewport.x, viewport.y, viewport.width, viewport.height},
            .lightRanges           = execution.renderData.lightRanges,
            .maxRecursionDepth     = pipelineRecursionDepth,
        }
    );
    const auto extent = execution.setup.swapchain.extent();
    commands.traceRaysKHR(raygenRegion, missRegion, hitRegion, {}, extent.width, extent.height, 1);
}
