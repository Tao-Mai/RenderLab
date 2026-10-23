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
    struct RayTracingParameters
    {
        glm::vec4   viewport;
        LightRanges lightRanges;
        uint32_t    maxRecursionDepth;
        uint32_t    sampleIndex;
        uint32_t    iblEnabled;
        uint32_t    padding = 0;
    };
    static_assert(sizeof(RayTracingParameters) == 48);
    static_assert(offsetof(RayTracingParameters, lightRanges) == 16);
    static_assert(offsetof(RayTracingParameters, maxRecursionDepth) == 32);
    static_assert(offsetof(RayTracingParameters, iblEnabled) == 40);

    RenderGraph::ImageHandle                 rayImage;
    RenderGraph::ImageHandle                 depth;
    RenderGraph::ImageHandle                 worldNormal;
    uint32_t                                 sampleIndex = 0;
    DescriptorSetLayout                      descriptorSetLayout;
    vk::PipelineLayout                       pipelineLayout;
    vk::raii::Pipeline                       pipeline               = nullptr;
    uint32_t                                 pipelineRecursionDepth = 0;
    Buffer                                   bindingTable;
    vk::StridedDeviceAddressRegionKHR        raygenRegion, missRegion, hitRegion;
    RenderGraph::AccelerationStructureHandle sceneTlas;
    RenderGraph::BufferHandle                hitData;
    std::array<Frame, maxFramesInFlight>     frames;

    void createPipeline(const PassSetup& setup, uint32_t maxRecursionDepth);
    void prepareHits(RenderGraph& graph, const PassFrame& execution, Frame& frame);
    void updateTlasDescriptor(RenderGraph& graph, const PassFrame& execution, Frame& frame);
};
