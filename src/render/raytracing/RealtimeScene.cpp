#include "render/raytracing/RealtimeScene.h"
#include "render/device/ResourceBarriers.h"

#include "core/Vertex.h"
#include "render/FrameData.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/device/VulkanContext.h"
#include "render/raytracing/RealtimeSceneData.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderLayout.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace
{
struct PrepareParameters
{
    uint32_t triangleCount;
    uint32_t taskCount;
};
static_assert(sizeof(PrepareParameters) == 8);
}  // namespace

RealtimeScene::Buffers RealtimeScene::compile(const Falcor::RenderContext& context, VulkanResources::BufferHandle triangles)
{
    auto& cache  = *context.vulkanResources;
    auto& device = *context.getDevice();
    constexpr auto kStorage = Falcor::ResourceBindFlags::UnorderedAccess;
    objectBuffer = device.createBuffer(sizeof(RayTracingHitData), kStorage);
    taskBuffer = device.createBuffer(sizeof(EmissiveTriangleTask), kStorage);
    buffers.objects = cache.registerBuffer(objectBuffer);
    tasks = cache.registerBuffer(taskBuffer);
    if (triangles.id == VulkanResources::kInvalidHandle)
    {
        triangleBuffer = device.createBuffer(sizeof(EmissiveTriangleHeader) + sizeof(EmissiveTriangle), kStorage);
        triangles = cache.registerBuffer(triangleBuffer);
    }
    else
        triangleBuffer.reset();
    buffers.triangles = triangles;

    accesses.clear();
    constexpr auto kCompute = vk::PipelineStageFlagBits2::eComputeShader;
    accesses.add(buffers.objects, vulkan_access::storageRead(kCompute));
    accesses.add(tasks, vulkan_access::storageRead(kCompute));
    accesses.add(buffers.triangles, {kCompute, vk::AccessFlagBits2::eShaderStorageWrite});

    accesses.declare(cache);

    triangleCount = 0;
    taskCount     = 0;
    if (!*pipeline)
    {
        createPipeline(device, *context.shaderManager);
    }
    for (auto& frame : frames)
    {
        frame.revisions = {};
    }

    return buffers;
}

void RealtimeScene::createPipeline(Falcor::Device& device, ShaderManager& shaders)
{
    std::array<vk::DescriptorSetLayoutBinding, 3> bindings;
    for (uint32_t binding = 0; binding < bindings.size(); ++binding)
    {
        bindings[binding] = {
            .binding         = binding,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eCompute,
        };
    }
    descriptorSetLayout = device.createDescriptorSetLayout(bindings);
    const vk::PushConstantRange range{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .size       = sizeof(PrepareParameters),
    };
    pipelineLayout    = device.getOrCreatePipelineLayout(std::array{&descriptorSetLayout}, std::array{range});
    const auto shader = shaders.getOrLoad("prepare_emissive_triangles");
    pipeline          = device.createComputePipeline(shader, pipelineLayout);
    for (auto& frame : frames)
    {
        frame.descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);
    }
}

void RealtimeScene::prepare(VulkanResources& cache, const Falcor::RenderContext& frame)
{
    const auto& items     = frame.scene->drawItems;
    auto&       resources = *frame.resources;
    const auto& materials = *frame.materialTable;
    static_assert(sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24);

    const auto upload =
        cache.allocateUpload(buffers.objects, std::max<size_t>(items.size(), 1) * sizeof(RayTracingHitData));
    auto*                             hits = reinterpret_cast<RayTracingHitData*>(upload.data.data());
    std::vector<EmissiveTriangleTask> lightTasks;
    triangleCount = 0;
    for (uint32_t index = 0; index < items.size(); ++index)
    {
        const auto&       item          = items[index];
        const auto&       mesh          = resources.mesh(item.mesh);
        const auto&       submesh       = mesh.submeshes()[item.submeshIndex];
        const auto        materialIndex = frame.scene->materialIndices[index];
        RayTracingHitData hit{
            .vertexAddress = mesh.vertices().getGpuAddress(),
            .indexAddress  = mesh.indices().getGpuAddress(),
            .firstIndex    = submesh.firstIndex,
            .materialIndex = materialIndex,
        };
        if (materials.info(materialIndex).emissive && submesh.indexCount > 0)
        {
            const auto count = submesh.indexCount / 3;
            CHECK(
                count < std::numeric_limits<uint32_t>::max() - triangleCount,
                "emissive triangle count exceeds the shader index range"
            );
            hit.firstEmissiveTriangle = triangleCount;
            lightTasks.push_back({item.model, index, triangleCount, count});
            triangleCount += count;
        }
        std::construct_at(hits + index, hit);
    }
    if (items.empty())
    {
        std::construct_at(hits, RayTracingHitData{});
    }

    taskCount = static_cast<uint32_t>(lightTasks.size());
    const auto taskStorageUpload =
        cache.allocateUpload(tasks, std::max<size_t>(taskCount, 1) * sizeof(EmissiveTriangleTask));
    if (taskCount > 0)
    {
        std::memcpy(taskStorageUpload.data.data(), lightTasks.data(), lightTasks.size() * sizeof(EmissiveTriangleTask));
    }
    else
    {
        std::memset(taskStorageUpload.data.data(), 0, sizeof(EmissiveTriangleTask));
    }
    cache.ensureBufferCapacity(
        buffers.triangles,
        sizeof(EmissiveTriangleHeader) + vk::DeviceSize(triangleCount) * sizeof(EmissiveTriangle)
    );
    const EmissiveTriangleHeader header{triangleCount};
    const auto                   headerUpload = cache.allocateUpload(buffers.triangles, sizeof(header));
    std::memcpy(headerUpload.data.data(), &header, sizeof(header));
    updateDescriptors(cache, frame);
}

void RealtimeScene::updateDescriptors(VulkanResources& cache, const Falcor::RenderContext& execution)
{
    auto&                                   frame = frames[execution.frameIndex];
    const std::array                        handles{buffers.objects, tasks, buffers.triangles};
    std::array<vk::DescriptorBufferInfo, 3> infos;
    std::array<vk::WriteDescriptorSet, 3>   writes;
    uint32_t                                writeCount = 0;
    for (uint32_t binding = 0; binding < handles.size(); ++binding)
    {
        const auto revision = cache.bufferRevision(handles[binding]);
        if (frame.revisions[binding] == revision)
        {
            continue;
        }
        const auto& buffer       = cache.getBuffer(handles[binding]);
        infos[binding]           = vk::DescriptorBufferInfo{.buffer = buffer.handle(), .range = buffer.getSize()};
        auto& write              = writes[writeCount++];
        write                    = makeDescriptorWrite(*frame.descriptorSet, descriptorSetLayout.bindings[binding]);
        write.pBufferInfo        = &infos[binding];
        frame.revisions[binding] = revision;
    }
    execution.vulkan->deviceHandle().updateDescriptorSets(std::span{writes.data(), writeCount}, {});
}

void RealtimeScene::execute(VulkanResources& cache, vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame)
{
    if (triangleCount == 0)
    {
        return;
    }
    accesses.transition(cache, commands);
    const auto groups = pipeline.groupCount({triangleCount, 1, 1});
    CHECK(
        groups.width <= frame.vulkan->properties().limits.maxComputeWorkGroupCount[0],
        "emissive triangle preparation requires {} compute groups",
        groups.width
    );

    commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commands.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipelineLayout,
        0,
        *frames[frame.frameIndex].descriptorSet,
        {}
    );
    commands.pushConstants<PrepareParameters>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eCompute,
        0,
        PrepareParameters{triangleCount, taskCount}
    );
    commands.dispatch(groups.width, groups.height, groups.depth);
}
