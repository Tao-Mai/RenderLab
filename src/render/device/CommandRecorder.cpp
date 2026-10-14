#include "render/device/CommandRecorder.h"

#include "core/profiling/Profiler.h"

void CommandRecorder::draw(
    uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance
) const
{
    vk::raii::CommandBuffer::draw(vertexCount, instanceCount, firstVertex, firstInstance);
    addStatistics({.drawCalls = 1, .triangles = static_cast<uint64_t>(vertexCount / 3) * instanceCount});
}

void CommandRecorder::drawIndexed(
    uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance
) const
{
    vk::raii::CommandBuffer::drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    addStatistics({.drawCalls = 1, .triangles = static_cast<uint64_t>(indexCount / 3) * instanceCount});
}

void CommandRecorder::addStatistics(DrawStatistics counts) const
{
    if (!statistics)
        return;

    statistics->drawCalls += counts.drawCalls;
    statistics->triangles += counts.triangles;
}
