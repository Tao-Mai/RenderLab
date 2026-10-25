#pragma once

#include "render/raytracing/RayTracingPass.h"

class TemporalReprojectionPass
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
        // RG = E[luminance], E[luminance^2]; two temporal ping-pong layers.
        RenderGraph::ImageHandle moments;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, RayTracingPass::Output input);
    void   writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const;
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame);

private:
    RayTracingPass::Output                  input;
    RenderGraph::ImageHandle                color;
    std::array<RenderGraph::ImageHandle, 4> history;
    vk::Extent3D                            extent{};
    DescriptorSetLayout                     descriptorSetLayout;
    vk::PipelineLayout                      pipelineLayout;
    vk::raii::Pipeline                      pipeline      = nullptr;
    vk::raii::DescriptorSet                 descriptorSet = nullptr;
    glm::mat4                               previousViewProjection{1.0f};
    glm::vec4                               previousViewport{};
    glm::vec4                               previousDepthRow{};
    uint32_t                                writeLayer = 0;
    bool                                    hasHistory = false;
    uint32_t                                previousMaxBounces = 0;

    void createPipeline(const VulkanContext& vulkan);
};
