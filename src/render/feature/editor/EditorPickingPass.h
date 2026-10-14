#pragma once

#include "render/ShaderManager.h"
#include "render/feature/RenderGraph.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_raii.hpp>

class GpuMesh;
class PipelineManager;
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
    void   execute(RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& execution) const;

private:
    void                     draw(CommandRecorder& commandBuffer, const DrawItem& item) const;
    RenderGraph::ImageHandle depth;
    RenderGraph::ImageHandle pickingImage;
    PipelineManager*         pipelines = nullptr;
    ShaderHandle             shader;
    vk::PipelineLayout       pipelineLayout;
    vk::Pipeline             pipeline;
};
