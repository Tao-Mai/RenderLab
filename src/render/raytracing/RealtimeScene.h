#pragma once

#include "render/device/Device.h"
#include "render/graph/RenderGraph.h"

#include <array>

// Prepares shared hit records and expands emissive instances into world-space triangle lights.
class RealtimeScene
{
public:
    struct Buffers
    {
        RenderGraph::BufferHandle objects;
        RenderGraph::BufferHandle triangles;
    };

    [[nodiscard]] Buffers compile(RenderGraph& graph, const PassSetup& setup);
    void prepare(RenderGraph& graph, const PassFrame& frame);
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame);

    [[nodiscard]] uint32_t emissiveTriangleCount() const { return triangleCount; }

private:
    struct Frame
    {
        vk::raii::DescriptorSet descriptorSet = nullptr;
        std::array<uint64_t, 3> revisions{};
    };

    Buffers                              buffers;
    RenderGraph::BufferHandle            tasks;
    uint32_t                             triangleCount = 0;
    uint32_t                             taskCount = 0;
    DescriptorSetLayout                  descriptorSetLayout;
    vk::PipelineLayout                   pipelineLayout;
    vk::raii::Pipeline                   pipeline = nullptr;
    std::array<Frame, maxFramesInFlight> frames;

    void createPipeline(const PassSetup& setup);
    void updateDescriptors(RenderGraph& graph, const PassFrame& frame);
};
