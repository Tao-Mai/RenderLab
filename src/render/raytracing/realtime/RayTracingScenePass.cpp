#include "render/raytracing/realtime/RayTracingScenePass.h"

#include "core/Vertex.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resources/GpuShader.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace
{
constexpr uint32_t kWorkgroupSize = 64;

struct PrepareParameters
{
    uint32_t triangleCount;
    uint32_t taskCount;
};
static_assert(sizeof(PrepareParameters) == 8);
}

RayTracingScenePass::Output RayTracingScenePass::setup(RenderGraph& graph, const PassSetup& setup)
{
    const auto createInput = [&](vk::DeviceSize size)
    {
        auto buffer = graph.createBuffer({
            .size  = size,
            .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        });
        graph.use(
            buffer,
            {
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageRead,
            }
        );
        return buffer;
    };
    output.objects   = createInput(sizeof(RayTracingHitData));
    tasks            = createInput(sizeof(EmissiveTriangleTask));
    output.triangles = graph.write(graph.createBuffer({
        .size  = sizeof(EmissiveTriangleHeader) + sizeof(EmissiveTriangle),
        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    }));
    graph.use(
        output.triangles,
        {
            .stages = vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eShaderStorageWrite,
        }
    );

    emissiveTriangleCount = 0;
    taskCount             = 0;
    if (!*pipeline)
    {
        createPipeline(setup);
    }
    for (auto& frame : frames)
    {
        frame.revisions = {};
    }
    return output;
}

void RayTracingScenePass::createPipeline(const PassSetup& setup)
{
    auto&                                         device = *renderContext().device;
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
    const auto&                   vulkan = setup.vulkan;
    GpuShader                     shader{vulkan.deviceHandle(), "shaders/prepare_emissive_triangles.spv"};
    const vk::raii::PipelineCache cache{nullptr};
    pipeline = vkCheck(vulkan.deviceHandle().createComputePipeline(
        cache,
        {
            .stage  = {.stage = vk::ShaderStageFlagBits::eCompute, .module = shader.handle(), .pName = "computeMain"},
            .layout = pipelineLayout,
        }
    ));
    for (auto& frame : frames)
    {
        frame.descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);
    }
}

void RayTracingScenePass::prepare(RenderGraph& graph, const PassFrame& frame)
{
    const auto& items     = frame.renderData.drawItems;
    const auto& device    = frame.setup.vulkan.deviceHandle();
    auto&       resources = *renderContext().resources;
    const auto& materials = *renderContext().materialTable;
    static_assert(sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24);

    const auto upload =
        graph.allocateUpload(output.objects, std::max<size_t>(items.size(), 1) * sizeof(RayTracingHitData));
    auto*                             hits = reinterpret_cast<RayTracingHitData*>(upload.data.data());
    std::vector<EmissiveTriangleTask> lightTasks;
    emissiveTriangleCount = 0;
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
                count < std::numeric_limits<uint32_t>::max() - emissiveTriangleCount,
                "emissive triangle count exceeds the shader index range"
            );
            hit.firstEmissiveTriangle = emissiveTriangleCount;
            lightTasks.push_back({item.model, index, emissiveTriangleCount, count});
            emissiveTriangleCount += count;
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
        output.triangles,
        sizeof(EmissiveTriangleHeader) + vk::DeviceSize(emissiveTriangleCount) * sizeof(EmissiveTriangle)
    );
    const EmissiveTriangleHeader header{emissiveTriangleCount};
    const auto                   headerUpload = graph.allocateUpload(output.triangles, sizeof(header));
    std::memcpy(headerUpload.data.data(), &header, sizeof(header));
    updateDescriptors(graph, frame);
}

void RayTracingScenePass::updateDescriptors(RenderGraph& graph, const PassFrame& execution)
{
    auto&                                   frame = frames[execution.frameIndex];
    const std::array                        handles{output.objects, tasks, output.triangles};
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
        writes[writeCount++] = {
            .dstSet          = *frame.descriptorSet,
            .dstBinding      = binding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &infos[binding],
        };
        frame.revisions[binding] = revision;
    }
    execution.setup.vulkan.deviceHandle().updateDescriptorSets(std::span{writes.data(), writeCount}, {});
}

void RayTracingScenePass::execute(RenderGraph&, vk::raii::CommandBuffer& commands, const PassFrame& frame) const
{
    if (emissiveTriangleCount == 0)
    {
        return;
    }
    const auto groups = (emissiveTriangleCount + kWorkgroupSize - 1) / kWorkgroupSize;
    CHECK(
        groups <= frame.setup.vulkan.properties().limits.maxComputeWorkGroupCount[0],
        "emissive triangle preparation requires {} compute groups",
        groups
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
        PrepareParameters{emissiveTriangleCount, taskCount}
    );
    commands.dispatch(groups, 1, 1);
}
