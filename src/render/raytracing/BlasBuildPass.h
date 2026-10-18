#pragma once

#include "render/feature/RenderGraph.h"
#include "render/raytracing/AccelerationStructure.h"

#include <map>
#include <tuple>

class GpuMesh;
struct DrawItem;

class BlasBuildPass
{
public:
    RenderGraph::AccelerationStructureHandle setup(RenderGraph& graph, const PassSetup& setup);
    void execute(RenderGraph& graph, CommandRecorder& commands, const PassFrame& frame);

    [[nodiscard]] const AccelerationStructure& mesh(const DrawItem& item) const;
    [[nodiscard]] const AccelerationStructure& rectLight() const { return rectLightBlas; }

private:
    std::map<std::tuple<const GpuMesh*, uint32_t, uint32_t>, AccelerationStructure> meshes;
    Buffer                                                                          rectLightVertices;
    Buffer                                                                          rectLightIndices;
    AccelerationStructure                                                           rectLightBlas;

    void createRectLightGeometry(CommandRecorder& commands, const VulkanContext& vulkan);
};
