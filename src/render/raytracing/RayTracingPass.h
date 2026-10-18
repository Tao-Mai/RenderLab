#pragma once

#include "render/RenderConfig.h"
#include "render/feature/RenderGraph.h"
#include "render/raytracing/RayTracingData.h"
#include "render/raytracing/TlasBuildPass.h"
#include "render/resource/ShaderData.h"

#include <array>
#include <glm/vec4.hpp>

class RayTracingPass
{
public:
    RayTracingPass()                                 = default;
    RayTracingPass(const RayTracingPass&)            = delete;
    RayTracingPass& operator=(const RayTracingPass&) = delete;
    RayTracingPass(RayTracingPass&&)                 = delete;
    RayTracingPass& operator=(RayTracingPass&&)      = delete;

    struct Output
    {
        RenderGraph::ImageHandle rayImage;
        RenderGraph::ImageHandle depth;
        RenderGraph::ImageHandle worldNormal;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup);
    void   execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution);

private:
    struct Frame
    {
        Buffer                  hitData;
        Buffer                  lightData;
        Buffer                  materialData;
        vk::raii::DescriptorSet set = nullptr;
    };
    struct RayTracingParameters
    {
        glm::mat4   inverseViewProjection;
        glm::vec4   cameraPosition;
        glm::vec4   viewport;
        LightRanges lightRanges;
        uint32_t    maxRecursionDepth;
        uint32_t    sampleIndex;
        uint32_t    padding[2]{};
    };
    static_assert(sizeof(RayTracingParameters) == 128);
    static_assert(offsetof(RayTracingParameters, lightRanges) == 96);
    static_assert(offsetof(RayTracingParameters, maxRecursionDepth) == 112);

    RenderGraph::ImageHandle             rayImage;
    RenderGraph::ImageHandle             depth;
    RenderGraph::ImageHandle             worldNormal;
    uint32_t                             sampleIndex            = 0;
    vk::raii::DescriptorSetLayout        descriptorLayout       = nullptr;
    vk::raii::DescriptorPool             descriptorPool         = nullptr;
    vk::raii::PipelineLayout             pipelineLayout         = nullptr;
    vk::raii::Pipeline                   pipeline               = nullptr;
    uint32_t                             pipelineRecursionDepth = 0;
    Buffer                               bindingTable;
    vk::StridedDeviceAddressRegionKHR    raygenRegion, missRegion, hitRegion;
    BlasBuildPass                        bottomLevels;
    TlasBuildPass                        topLevel{bottomLevels};
    std::array<Frame, maxFramesInFlight> frames;

    void createPipeline(const PassSetup& setup, uint32_t maxRecursionDepth);
    void prepareLights(const PassFrame& execution, Frame& frame) const;
    void prepareBindings(RenderGraph& graph, const PassFrame& execution, Frame& frame);
};
