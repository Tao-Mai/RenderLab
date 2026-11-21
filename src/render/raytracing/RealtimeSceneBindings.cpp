#include "render/raytracing/RealtimeSceneBindings.h"

#include "core/Context.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/device/GpuProfiler.h"
#include "render/device/VulkanContext.h"
#include "render/raytracing/AccelerationStructure.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderData.h"

#include <tuple>

void RealtimeSceneBindings::use(
    RenderGraph& graph, const ShaderLayout& shader, RealtimeScene::Buffers geometry, RenderGraph::BufferHandle lights
)
{
    constexpr auto kSet = RenderInterface::kRayTracingPassSet;
    tlasBinding = shader.binding<vk::DescriptorType::eAccelerationStructureKHR>("scene", kSet);
    objectsBinding = shader.binding<vk::DescriptorType::eStorageBuffer>("objects", kSet);
    trianglesBinding = shader.binding<vk::DescriptorType::eStorageBuffer>("emissiveTriangles", kSet);

    for (const auto buffer : {geometry.objects, geometry.triangles, lights})
        graph.use(buffer, {
            .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            .access = vk::AccessFlagBits2::eShaderStorageRead,
        });
    renderContext().materialTable->use(graph, vk::PipelineStageFlagBits2::eRayTracingShaderKHR);

    buffers = geometry;
    frames = {};
}

void RealtimeSceneBindings::appendLayoutBindings(std::vector<vk::DescriptorSetLayoutBinding>& bindings) const
{
    bindings.push_back(tlasBinding.layout(vk::ShaderStageFlagBits::eRaygenKHR));
    bindings.push_back(objectsBinding.layout(vk::ShaderStageFlagBits::eClosestHitKHR));
    bindings.push_back(trianglesBinding.layout(vk::ShaderStageFlagBits::eRaygenKHR));
}

vk::PipelineLayout RealtimeSceneBindings::getPipelineLayout(
    Device& device, const ShaderLayout& shader, const DescriptorSetLayout& passLayout,
    std::span<const vk::PushConstantRange> ranges
)
{
    const std::array<const DescriptorSetLayout*, 4> layouts{
        &device.sceneDescriptorSetLayout,
        &device.lightDescriptorSetLayout,
        &device.materialDescriptorSetLayout,
        &passLayout,
    };
    shader.validateDescriptorSets(layouts);
    return device.getOrCreatePipelineLayout(layouts, ranges);
}

void RealtimeSceneBindings::prepare(RenderGraph& graph, const PassFrame& frame, vk::DescriptorSet passSet)
{
    const std::array bindings{objectsBinding, trianglesBinding};
    const std::array handles{buffers.objects, buffers.triangles};
    auto& revisions = frames[frame.frameIndex].bufferRevisions;
    std::array<vk::DescriptorBufferInfo, std::tuple_size_v<decltype(bindings)>> infos;
    std::array<vk::WriteDescriptorSet, std::tuple_size_v<decltype(bindings)>> writes;
    uint32_t writeCount = 0;
    for (uint32_t index = 0; index < handles.size(); ++index)
    {
        const auto revision = graph.bufferRevision(handles[index]);
        if (revisions[index] == revision)
            continue;

        const auto& buffer = graph.getBuffer(handles[index]);
        infos[index] = vk::DescriptorBufferInfo{.buffer = buffer.handle(), .range = buffer.size()};
        auto& write = writes[writeCount++];
        write = bindings[index].write(passSet);
        write.pBufferInfo = &infos[index];
        revisions[index] = revision;
    }
    if (writeCount != 0)
        frame.setup.vulkan.deviceHandle().updateDescriptorSets(std::span{writes.data(), writeCount}, {});
}

void RealtimeSceneBindings::bind(
    vk::raii::CommandBuffer& commands, const PassFrame& frame, vk::PipelineLayout layout, vk::DescriptorSet passSet
)
{
    // Renderer records TLAS builds after frame preparation.
    {
        CpuProfileScope timing{context().profiler, "RT.Binding preparation"};
        GpuProfileScope gpuTiming{frame.gpuProfiler, commands, "RT.Binding preparation"};
        updateTlas(frame, passSet);
    }
    const std::array sets{
        frame.data.sceneSetHandle(),
        frame.data.lightSetHandle(),
        frame.renderData.materialSetHandle(),
        passSet,
    };
    commands.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, layout, 0, sets, {});
}

void RealtimeSceneBindings::updateTlas(const PassFrame& frame, vk::DescriptorSet passSet)
{
    DCHECK(frame.sceneTlas);
    const auto& structure = *frame.sceneTlas;
    auto& revision = frames[frame.frameIndex].tlasRevision;
    if (revision == structure.revision())
        return;

    const auto tlas = structure.handle();
    const vk::WriteDescriptorSetAccelerationStructureKHR info{
        .accelerationStructureCount = 1,
        .pAccelerationStructures = &tlas,
    };
    auto write = tlasBinding.write(passSet);
    write.pNext = &info;
    frame.setup.vulkan.deviceHandle().updateDescriptorSets(write, {});
    revision = structure.revision();
}
