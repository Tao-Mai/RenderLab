#pragma once

#include "render/graph/RenderPass.h"

#include "render/device/Device.h"

#include "render/resources/ShaderManager.h"
#include "render/graph/RenderGraph.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_raii.hpp>

class GpuMesh;
class Device;
class RenderGraph;
struct DrawItem;

class EditorPickingPass : public RenderPass
{
public:

    [[nodiscard]] RenderPassReflection reflect(const PassSetup& setup) const override;
    void compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports) override;
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) override;

private:
    void                     draw(vk::raii::CommandBuffer& commandBuffer, const DrawItem& item) const;
    RenderGraph::ImageHandle depth;
    RenderGraph::ImageHandle pickingImage;
    ShaderHandle             shader;
    vk::PipelineLayout       pipelineLayout;
    vk::Pipeline             pipeline;
};
