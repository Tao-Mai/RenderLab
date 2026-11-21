#pragma once

#include "render/device/ResourceBarriers.h"
#include "passes/denoise/SVGF/SVGFReprojection.h"

#include <array>
#include <vector>

inline constexpr uint32_t kSvgfFilterIterations = 5;

class SVGFFilter
{
public:
    void compile(
        const Falcor::RenderContext& context,
        const DenoiserInputs& traced,
        const SVGFReprojection::Output& lighting,
        const std::array<VulkanResources::ImageHandle, kLightingLayerCount>& scratch,
        VulkanResources::ImageHandle color,
        const SVGFReprojection& reprojection
    );
    void execute(VulkanResources& cache, vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame) const;

private:
    struct FilterResources
    {
        VulkanResources::ImageHandle direct;
        VulkanResources::ImageHandle indirect;
        VulkanResources::ImageHandle targetDirect;
        VulkanResources::ImageHandle targetIndirect;
        VulkanResources::ImageHandle color;
        VulkanResources::ImageHandle depth;
        VulkanResources::ImageHandle normal;
        VulkanResources::ImageHandle emission;
        VulkanResources::ImageHandle albedo;
        VulkanResources::ImageHandle lightingHistory;
    };

    class FilterPass
    {
    public:
        FilterPass(uint32_t iteration, const SVGFReprojection& reprojection) :
            iteration(iteration), reprojection(reprojection)
        {
        }

        void compile(const Falcor::RenderContext& context, const FilterResources& resources);
        void execute(VulkanResources& cache, vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame) const;

    private:
        uint32_t                iteration;
        const SVGFReprojection& reprojection;
        ResourceBarriers    accesses;
        vk::Extent3D            extent{};
        DescriptorSetLayout     descriptorSetLayout;
        vk::PipelineLayout      pipelineLayout;
        ComputePipeline         pipeline;
        vk::raii::DescriptorSet descriptorSet = nullptr;

        [[nodiscard]] bool first() const { return iteration == 0; }
        [[nodiscard]] bool last() const { return iteration + 1 == kSvgfFilterIterations; }
        void               createPipeline(const Falcor::RenderContext& context);
    };

    std::vector<FilterPass> filters;
};
