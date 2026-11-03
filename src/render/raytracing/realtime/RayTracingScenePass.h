#pragma once

#include "render/device/Device.h"
#include "render/graph/RenderGraph.h"
#include "render/raytracing/realtime/RayTracingData.h"

#include <array>

// Prepares shared hit records and expands emissive instances into world-space triangle lights.
class RayTracingScenePass
{
public:
    struct Output
    {
        RenderGraph::BufferHandle objects;
        RenderGraph::BufferHandle triangles;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup);
    void   prepare(RenderGraph& graph, const PassFrame& frame);
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame) const;

    [[nodiscard]] uint32_t triangleCount() const { return emissiveTriangleCount; }

private:
    struct Frame
    {
        vk::raii::DescriptorSet descriptorSet = nullptr;
        std::array<uint64_t, 3> revisions{};
    };

    Output                               output;
    RenderGraph::BufferHandle            tasks;
    uint32_t                             emissiveTriangleCount = 0;
    uint32_t                             taskCount             = 0;
    DescriptorSetLayout                  descriptorSetLayout;
    vk::PipelineLayout                   pipelineLayout;
    vk::raii::Pipeline                   pipeline = nullptr;
    std::array<Frame, maxFramesInFlight> frames;

    void createPipeline(const PassSetup& setup);
    void updateDescriptors(RenderGraph& graph, const PassFrame& frame);
};
