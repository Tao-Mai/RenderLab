#pragma once

#include "render/raytracing/RayTracingPass.h"

class SVGFReprojection
{
public:
    struct Output
    {
        // RGBA32F: RGB accumulated albedo-demodulated color, A its estimated luminance variance.
        RenderGraph::ImageHandle direct;
        RenderGraph::ImageHandle indirect;
        // 亮度矩和亮度二阶矩: RG = E[L], E[L^2], B = sample count.
        RenderGraph::ImageHandle directMoments;
        RenderGraph::ImageHandle indirectMoments;
        // RGB color history is written by the first wavelet iteration, not by reprojection.
        RenderGraph::ImageHandle directHistory;
        RenderGraph::ImageHandle indirectHistory;
    };

    Output setup(RenderGraph& graph, const PassSetup& setup, RayTracingPass::Output input);
    void   writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const;
    void   execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame);

    // Layer belonging to the frame most recently executed by this pass.
    [[nodiscard]] uint32_t colorHistoryLayer() const { return writeLayer ^ 1u; }

private:
    RayTracingPass::Output                  input;
    Output                                  output;
    std::array<RenderGraph::ImageHandle, 6> history;
    vk::Extent3D                            extent{};
    DescriptorSetLayout                     descriptorSetLayout;
    vk::PipelineLayout                      pipelineLayout;
    vk::raii::Pipeline                      pipeline      = nullptr;
    vk::raii::DescriptorSet                 descriptorSet = nullptr;
    glm::mat4                               previousViewProjection{1.0f};
    glm::vec4                               previousViewport{};
    glm::vec4                               previousDepthRow{};
    uint32_t                                writeLayer         = 0;
    bool                                    hasHistory         = false;
    uint32_t                                previousMaxBounces = 0;

    void createPipeline(const VulkanContext& vulkan);
};
