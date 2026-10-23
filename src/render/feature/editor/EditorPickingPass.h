#pragma once

#include "render/device/Device.h"

#include "render/ShaderManager.h"
#include "render/feature/RenderGraph.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_raii.hpp>

class GpuMesh;
class Device;
class RenderGraph;
struct DrawItem;

class EditorPickingPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle pickingImage;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle depthHandle);
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const;

private:
    void                     draw(vk::raii::CommandBuffer& commandBuffer, const DrawItem& item) const;
    RenderGraph::ImageHandle depth;
    RenderGraph::ImageHandle pickingImage;
    ShaderHandle             shader;
    vk::PipelineLayout       pipelineLayout;
    vk::Pipeline             pipeline;
};
