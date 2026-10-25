#pragma once

#include "render/RenderConfig.h"
#include "render/device/Device.h"
#include "render/feature/RenderGraph.h"
#include "render/raytracing/RayTracingData.h"
#include "render/raytracing/TlasBuildPass.h"
#include "render/resource/ShaderData.h"

#include <array>
#include <glm/vec4.hpp>

class RayTracingPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle rayImage;
        RenderGraph::ImageHandle depth;
        RenderGraph::ImageHandle worldNormal;
    };

    Output
    setup(RenderGraph& graph, const PassSetup& setup, TlasBuildPass::Output scene, RenderGraph::BufferHandle lights);
    void writeDescriptorSet(RenderGraph& graph, const PassSetup& setup);
    void prepare(RenderGraph& graph, const PassFrame& execution);
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution);

private:
    struct Frame
    {
        uint64_t                hitBufferRevision = 0;
        vk::raii::DescriptorSet set               = nullptr;
        uint64_t                tlasRevision      = 0;
    };
    RenderGraph::ImageHandle                 rayImage;
    RenderGraph::ImageHandle                 depth;
    RenderGraph::ImageHandle                 worldNormal;
    uint32_t                                 sampleIndex = 0;
    DescriptorSetLayout                      descriptorSetLayout;
    vk::PipelineLayout                       pipelineLayout;
    vk::raii::Pipeline                       pipeline = nullptr;
    Buffer                                   bindingTable;
    vk::StridedDeviceAddressRegionKHR        raygenRegion, missRegion, hitRegion;
    RenderGraph::AccelerationStructureHandle sceneTlas;
    RenderGraph::BufferHandle                hitData;
    std::array<Frame, maxFramesInFlight>     frames;

    void createPipeline(const PassSetup& setup);
    void prepareHits(RenderGraph& graph, const PassFrame& execution, Frame& frame);
    void updateTlasDescriptor(RenderGraph& graph, const PassFrame& execution, Frame& frame);
};
