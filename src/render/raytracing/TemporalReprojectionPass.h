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
    void prepare(RenderGraph& graph, const PassSetup& setup) const;
    void execute(RenderGraph& graph, CommandRecorder& commands, const PassFrame& frame);

private:
    struct HistoryImage
    {
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::Image image = nullptr;
        std::unique_ptr<Image> views;
        RenderGraph::ImageHandle handle;
        RenderGraph::ImageAccess state;
    };

    RayTracingPass::Output input;
    RenderGraph::ImageHandle color;
    std::array<std::unique_ptr<HistoryImage>, 4> history;
    vk::Extent3D extent{};
    vk::raii::DescriptorSetLayout descriptorLayout = nullptr;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;
    vk::raii::DescriptorPool descriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> sets;
    glm::mat4 previousViewProjection{1.0f};
    glm::vec4 previousViewport{};
    glm::vec4 previousDepthRow{};
    uint32_t writeLayer = 0;
    bool hasHistory = false;

    void createPipeline(const VulkanContext& vulkan);
};
