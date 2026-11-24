#pragma once
#include <slang-rhi.h>

#include "passes/denoise/SVGF/SVGFReprojection.h"

#include <array>
#include <vector>

inline constexpr uint32_t kSvgfFilterIterations = 5;

class SVGFFilter
{
public:
    void compile(
        const RenderContext&                                      context,
        const SVGFInputs&                                                 traced,
        const SVGFReprojection::Output&                                   lighting,
        const std::array<GpuResources::ImageHandle, kLightingLayerCount>& scratch,
        GpuResources::ImageHandle                                         color,
        const SVGFReprojection&                                           reprojection
    );
    void execute(GpuResources& cache, rhi::ICommandEncoder& commands, const RenderContext& frame) const;

private:
    struct FilterResources
    {
        GpuResources::ImageHandle direct;
        GpuResources::ImageHandle indirect;
        GpuResources::ImageHandle targetDirect;
        GpuResources::ImageHandle targetIndirect;
        GpuResources::ImageHandle color;
        GpuResources::ImageHandle depth;
        GpuResources::ImageHandle normal;
        GpuResources::ImageHandle emission;
        GpuResources::ImageHandle albedo;
        GpuResources::ImageHandle lightingHistory;
    };

    class FilterPass
    {
    public:
        FilterPass(uint32_t iteration, const SVGFReprojection& reprojection) :
            iteration(iteration), reprojection(reprojection)
        {
        }

        void compile(const RenderContext& context, const FilterResources& resources);
        void execute(GpuResources& cache, rhi::ICommandEncoder& commands, const RenderContext& frame) const;

    private:
        uint32_t                iteration;
        const SVGFReprojection& reprojection;

        rhi::Extent3D                      extent{};
        rhi::ComPtr<rhi::IComputePipeline> pipeline;
        rhi::ComPtr<rhi::IShaderObject>    shaderObject;

        [[nodiscard]] bool first() const { return iteration == 0; }
        [[nodiscard]] bool last() const { return iteration + 1 == kSvgfFilterIterations; }
        void               createPipeline(const RenderContext& context);
    };

    std::vector<FilterPass> filters;
};
