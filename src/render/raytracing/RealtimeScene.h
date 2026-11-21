#pragma once

#include "render/device/Device.h"
#include "render/device/ResourceBarriers.h"

#include <array>

namespace Falcor
{
class RenderContext;
}  // namespace Falcor

// Prepares shared hit records and expands emissive instances into world-space triangle lights.
class RealtimeScene
{
public:
    struct Buffers
    {
        VulkanResources::BufferHandle objects;
        VulkanResources::BufferHandle triangles;
    };

    [[nodiscard]] Buffers compile(const Falcor::RenderContext& context, VulkanResources::BufferHandle triangles = {});
    void                  prepare(VulkanResources& cache, const Falcor::RenderContext& frame);
    void execute(VulkanResources& cache, vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame);

    [[nodiscard]] uint32_t emissiveTriangleCount() const { return triangleCount; }

private:
    struct Frame
    {
        vk::raii::DescriptorSet descriptorSet = nullptr;
        std::array<uint64_t, 3> revisions{};
    };

    Buffers                              buffers;
    Falcor::ref<Falcor::Buffer>                          objectBuffer;
    Falcor::ref<Falcor::Buffer>                          taskBuffer;
    Falcor::ref<Falcor::Buffer>                          triangleBuffer;
    ResourceBarriers                     accesses;
    VulkanResources::BufferHandle        tasks;
    uint32_t                             triangleCount = 0;
    uint32_t                             taskCount     = 0;
    DescriptorSetLayout                  descriptorSetLayout;
    vk::PipelineLayout                   pipelineLayout;
    ComputePipeline                      pipeline;
    std::array<Frame, maxFramesInFlight> frames;

    void createPipeline(Falcor::Device& device, ShaderManager& shaders);
    void updateDescriptors(VulkanResources& cache, const Falcor::RenderContext& frame);
};
