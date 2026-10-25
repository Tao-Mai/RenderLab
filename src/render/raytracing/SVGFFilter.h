#pragma once

#include "render/raytracing/SVGFReprojection.h"

#include <memory>
#include <vector>

inline constexpr uint32_t kSvgfFilterIterations = 5;

// Registers one RenderGraph compute pass per wavelet iteration.
class SVGFFilter
{
public:
    struct Output
    {
        RenderGraph::ImageHandle color;
    };

    Output setup(
        RenderGraph&             graph,
        const PassSetup&         setup,
        RayTracingPass::Output   traced,
        SVGFReprojection::Output lighting,
        const SVGFReprojection&  reprojection
    );

private:
    struct FilterInput
    {
        RenderGraph::ImageHandle direct;
        RenderGraph::ImageHandle indirect;
        RenderGraph::ImageHandle targetDirect;
        RenderGraph::ImageHandle targetIndirect;
        RenderGraph::ImageHandle depth;
        RenderGraph::ImageHandle normal;
        RenderGraph::ImageHandle emission;
        RenderGraph::ImageHandle albedo;
        RenderGraph::ImageHandle directHistory;
        RenderGraph::ImageHandle indirectHistory;
    };

    struct FilterOutput
    {
        RenderGraph::ImageHandle direct;
        RenderGraph::ImageHandle indirect;
        RenderGraph::ImageHandle color;
    };

    class FilterPass
    {
    public:
        FilterPass(uint32_t iteration, const SVGFReprojection& reprojection) :
            iteration(iteration), reprojection(reprojection)
        {
        }

        FilterOutput setup(RenderGraph& graph, const PassSetup& setup, FilterInput input);
        void         writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const;
        void         execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame) const;

    private:
        uint32_t                iteration;
        const SVGFReprojection& reprojection;
        FilterInput             input;
        FilterOutput            output;
        vk::Extent3D            extent{};
        DescriptorSetLayout     descriptorSetLayout;
        vk::PipelineLayout      pipelineLayout;
        vk::raii::Pipeline      pipeline      = nullptr;
        vk::raii::DescriptorSet descriptorSet = nullptr;

        [[nodiscard]] bool first() const { return iteration == 0; }
        [[nodiscard]] bool last() const { return iteration + 1 == kSvgfFilterIterations; }
        void               createPipeline(const VulkanContext& vulkan);
    };

    std::vector<std::unique_ptr<FilterPass>> filters;
};
