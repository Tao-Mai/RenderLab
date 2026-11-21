#include "render/raytracing/RealtimeScene.h"

#include "core/Vertex.h"
#include "render/raytracing/RealtimeSceneData.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/device/VulkanContext.h"
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
}

RealtimeScene::Buffers RealtimeScene::compile(RenderGraph& graph, const PassSetup&)
{
    const auto createUploadBuffer = [&](vk::DeviceSize size)
    {
        return graph.createBuffer({
            .size  = size,
            .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        });
    };
    buffers.objects = graph.write(createUploadBuffer(sizeof(RayTracingHitData)));
    tasks           = createUploadBuffer(sizeof(EmissiveTriangleTask));
    for (const auto buffer : {buffers.objects, tasks})
        graph.use(
            buffer,
            {
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageRead,
            }
        );

    buffers.triangles = graph.write(createUploadBuffer(sizeof(EmissiveTriangleHeader) + sizeof(EmissiveTriangle)));
    graph.use(
        buffers.triangles,
        {
            .stages = vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eShaderStorageWrite,
        }
    );

    triangleCount = 0;
    taskCount     = 0;
    if (!*pipeline)
    {
        createPipeline();
    }
    for (auto& frame : frames)
    {
        frame.revisions = {};
    }

    return buffers;
}

void RealtimeScene::createPipeline()
{
    auto& ctx = renderContext();
    auto& device = *ctx.device;
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
    pipelineLayout = device.getOrCreatePipelineLayout(std::array{&descriptorSetLayout}, std::array{range});
    const auto shader = ctx.shaderManager->getOrLoad("prepare_emissive_triangles");
    pipeline = device.createComputePipeline(shader, pipelineLayout);
    for (auto& frame : frames)
    {
        frame.descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);
    }
}

void RealtimeScene::prepare(RenderGraph& graph, const PassFrame& frame)
{
    const auto& items     = frame.renderData.drawItems;
    const auto& device    = frame.setup.vulkan.deviceHandle();
    auto&       resources = *renderContext().resources;
    const auto& materials = *renderContext().materialTable;
    static_assert(sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24);

    const auto upload =
        graph.allocateUpload(buffers.objects, std::max<size_t>(items.size(), 1) * sizeof(RayTracingHitData));
    auto*                             hits = reinterpret_cast<RayTracingHitData*>(upload.data.data());
    std::vector<EmissiveTriangleTask> lightTasks;
    triangleCount = 0;
    for (uint32_t index = 0; index < items.size(); ++index)
    {
        const auto&       item          = items[index];
        const auto&       mesh          = resources.mesh(item.mesh);
        const auto&       submesh       = mesh.submeshes()[item.submeshIndex];
        const auto        materialIndex = frame.renderData.materialIndices[index];
        RayTracingHitData hit{
            .vertexAddress = mesh.vertices().address(device),
            .indexAddress  = mesh.indices().address(device),
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

    taskCount             = static_cast<uint32_t>(lightTasks.size());
    const auto taskUpload = graph.allocateUpload(tasks, std::max<size_t>(taskCount, 1) * sizeof(EmissiveTriangleTask));
    if (taskCount > 0)
    {
        std::memcpy(taskUpload.data.data(), lightTasks.data(), lightTasks.size() * sizeof(EmissiveTriangleTask));
    }
    else
    {
        std::memset(taskUpload.data.data(), 0, sizeof(EmissiveTriangleTask));
    }
    graph.ensureBufferCapacity(
        buffers.triangles,
        sizeof(EmissiveTriangleHeader) + vk::DeviceSize(triangleCount) * sizeof(EmissiveTriangle)
    );
    const EmissiveTriangleHeader header{triangleCount};
    const auto                   headerUpload = graph.allocateUpload(buffers.triangles, sizeof(header));
    std::memcpy(headerUpload.data.data(), &header, sizeof(header));
    updateDescriptors(graph, frame);
}

void RealtimeScene::updateDescriptors(RenderGraph& graph, const PassFrame& execution)
{
    auto&                                   frame = frames[execution.frameIndex];
    const std::array                        handles{buffers.objects, tasks, buffers.triangles};
    std::array<vk::DescriptorBufferInfo, 3> infos;
    std::array<vk::WriteDescriptorSet, 3>   writes;
    uint32_t                                writeCount = 0;
    for (uint32_t binding = 0; binding < handles.size(); ++binding)
    {
        const auto revision = graph.bufferRevision(handles[binding]);
        if (frame.revisions[binding] == revision)
        {
            continue;
        }
        const auto& buffer   = graph.getBuffer(handles[binding]);
        infos[binding]       = vk::DescriptorBufferInfo{.buffer = buffer.handle(), .range = buffer.size()};
        auto& write = writes[writeCount++];
        write = makeDescriptorWrite(*frame.descriptorSet, descriptorSetLayout.bindings[binding]);
        write.pBufferInfo = &infos[binding];
        frame.revisions[binding] = revision;
    }
    execution.setup.vulkan.deviceHandle().updateDescriptorSets(std::span{writes.data(), writeCount}, {});
}

void RealtimeScene::execute(RenderGraph&, vk::raii::CommandBuffer& commands, const PassFrame& frame)
{
    if (triangleCount == 0)
    {
        return;
    }
    const auto groups = pipeline.groupCount({triangleCount, 1, 1});
    CHECK(
        groups.width <= frame.setup.vulkan.properties().limits.maxComputeWorkGroupCount[0],
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

