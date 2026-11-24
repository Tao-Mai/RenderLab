#pragma once
#include <slang-rhi.h>

#include "passes/denoise/SVGF/SVGFReprojection.h"

#include <array>
#include <vector>

inline constexpr uint32_t kSvgfFilterIterations = 5;

struct ViewportRect;

class SVGFFilter
{
public:
    void compile(
        rhi::IDevice&                                          device,
        ShaderManager&                                         shaders,
        const SVGFInputs&                                      traced,
        const SVGFReprojection::Output&                        lighting,
        const std::array<rhi::ITexture*, kLightingLayerCount>& scratch,
        rhi::ITexture*                                         color,
        const SVGFReprojection&                                reprojection
    );
    void execute(rhi::ICommandEncoder& commands, const ViewportRect& viewport) const;

private:
    struct FilterResources
    {
        rhi::ITexture* direct;
        rhi::ITexture* indirect;
        rhi::ITexture* targetDirect;
        rhi::ITexture* targetIndirect;
        rhi::ITexture* color;
        rhi::ITexture* depth;
        rhi::ITexture* normal;
        rhi::ITexture* emission;
        rhi::ITexture* albedo;
        rhi::ITexture* lightingHistory;
    };

    class FilterPass
    {
    public:
        FilterPass(uint32_t iteration, const SVGFReprojection& reprojection) :
            iteration(iteration), reprojection(reprojection)
        {
        }

        void compile(rhi::IDevice& device, ShaderManager& shaders, const FilterResources& resources);
        void execute(rhi::ICommandEncoder& commands, const ViewportRect& viewport) const;

    private:
        uint32_t                iteration;
        const SVGFReprojection& reprojection;

        rhi::Extent3D                      extent{};
        rhi::ComPtr<rhi::IComputePipeline> pipeline;
        rhi::ComPtr<rhi::IShaderObject>    shaderObject;

        [[nodiscard]] bool first() const { return iteration == 0; }
        [[nodiscard]] bool last() const { return iteration + 1 == kSvgfFilterIterations; }
        void               createPipeline(rhi::IDevice& device, ShaderManager& shaders);
    };

    std::vector<FilterPass> filters;
};
