#pragma once

#include "render/RenderConfig.h"
#include "render/feature/RenderGraph.h"
#include "render/raytracing/AccelerationStructure.h"

#include <array>
#include <glm/vec4.hpp>
#include <map>
#include <tuple>
#include <vector>

class RenderGraph;
class GpuMesh;

class RayTracingPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle rayImage;
    };

    Output setup(RenderGraph& graph);
    void   execute(RenderGraph& graph);

private:
    static constexpr uint32_t maxTextures = 128;
    struct Frame
    {
        AccelerationStructure   tlas;
        Buffer                  instances;
        Buffer                  hitData;
        Buffer                  lightData;
        vk::raii::DescriptorSet set           = nullptr;
        uint32_t                instanceCount = ~0u;
    };
    struct HitData
    {
        uint64_t  vertexAddress;
        uint64_t  indexAddress;
        glm::vec4 baseColorFactor;
        uint32_t  firstIndex;
        uint32_t  textureIndex;
        uint32_t  padding[2]{};
    };
    static_assert(sizeof(HitData) == 48);

    struct RayTracingParameters
    {
        glm::mat4 inverseViewProjection;
        glm::vec4 cameraPosition;
        glm::vec4 viewport;
        uint32_t  lightCount;
        uint32_t  padding[3]{};
    };
    static_assert(sizeof(RayTracingParameters) == 112);
    static_assert(offsetof(RayTracingParameters, lightCount) == 96);

    RenderGraph::ImageHandle                                                        rayImage;
    vk::raii::DescriptorSetLayout                                                   descriptorLayout = nullptr;
    vk::raii::DescriptorPool                                                        descriptorPool   = nullptr;
    vk::raii::PipelineLayout                                                        pipelineLayout   = nullptr;
    vk::raii::Pipeline                                                              pipeline         = nullptr;
    Buffer                                                                          bindingTable;
    vk::StridedDeviceAddressRegionKHR                                               raygenRegion, missRegion, hitRegion;
    std::map<std::tuple<const GpuMesh*, uint32_t, uint32_t>, AccelerationStructure> blas;
    std::array<Frame, maxFramesInFlight>                                            frames;

    void createPipeline(RenderGraph& graph);
    void prepareLights(RenderGraph& graph, Frame& frame) const;
    void prepareScene(RenderGraph& graph, Frame& frame);
};
