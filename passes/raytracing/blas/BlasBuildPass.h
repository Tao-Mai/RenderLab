#pragma once

#include "render/graph/RenderGraph.h"
#include "render/raytracing/AccelerationStructure.h"

#include <map>
#include <tuple>

class GpuMesh;

class BlasBuildPass
{
public:
    RenderGraph::AccelerationStructureHandle setup(RenderGraph& graph, const PassSetup& setup);
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame);

private:
    RenderGraph::AccelerationStructureHandle                                        output;
    std::map<std::tuple<const GpuMesh*, uint32_t, uint32_t>, AccelerationStructure> meshes;
    Buffer                                                                          rectLightVertices;
    Buffer                                                                          rectLightIndices;
    AccelerationStructure                                                           rectLightBlas;

    void createRectLightGeometry(vk::raii::CommandBuffer& commands, const VulkanContext& vulkan);
};
