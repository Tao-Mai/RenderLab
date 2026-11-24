#pragma once

#include "passes/denoise/SVGF/SvgfSettings.h"

#include <slang-rhi.h>
#include "render/denoise/DenoiserInputs.h"

#include <array>

namespace Falcor
{
struct RenderContext;
}  // namespace Falcor

class SVGFReprojection
{
    static constexpr size_t kHistoryCount = 4;

public:
    explicit SVGFReprojection(const SvgfSettings& settings) : settings(settings) {}

    struct Output
    {
        // RGBA32F: RGB accumulated albedo-demodulated color, A its estimated luminance variance.
        GpuResources::ImageHandle direct;
        GpuResources::ImageHandle indirect;
        // RGB color history is written by the first wavelet iteration, not by reprojection.
        GpuResources::ImageHandle lightingHistory;
    };

    Output compile(
        const Falcor::RenderContext&                                      context,
        const DenoiserInputs&                                             source,
        const std::array<GpuResources::ImageHandle, kHistoryCount>&       history,
        const std::array<GpuResources::ImageHandle, kLightingLayerCount>& outputs
    );
    void execute(GpuResources& cache, rhi::ICommandEncoder& commands, const Falcor::RenderContext& frame);

    // Layer belonging to the frame most recently executed by this pass.
    [[nodiscard]] uint32_t colorHistoryLayer() const { return writeLayer ^ 1u; }

private:
    GpuResources::ImageHandle historySource;

    rhi::Extent3D                      extent{};
    rhi::ComPtr<rhi::IComputePipeline> pipeline;
    rhi::ComPtr<rhi::IShaderObject>    shaderObject;
    glm::mat4                          previousViewProjection{1.0f};
    glm::vec4                          previousViewport{};
    glm::vec4                          previousDepthRow{};
    uint32_t                           writeLayer = 0;
    bool                               hasHistory = false;
    const SvgfSettings&                settings;
    uint64_t                           previousHistoryRevision = 0;

    void createPipeline(const Falcor::RenderContext& context);
};
