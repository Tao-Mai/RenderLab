#pragma once

#include "shaders/passes/denoise/SVGF/SvgfSettings.h"

#include <slang-rhi.h>
#include "shaders/raytracing/LightingLayers.h"

#include <array>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

class ShaderManager;
class GpuScene;

struct SVGFInputs
{
    rhi::ITexture* color;
    rhi::ITexture* depth;
    rhi::ITexture* worldNormal;
    rhi::ITexture* emission;
    rhi::ITexture* albedo;
};

class SVGFReprojection
{
    static constexpr size_t kHistoryCount = 4;

public:
    explicit SVGFReprojection(const SvgfSettings& settings) : settings(settings) {}

    struct Output
    {
        // RGBA32F: RGB accumulated albedo-demodulated color, A its estimated luminance variance.
        rhi::ITexture* direct;
        rhi::ITexture* indirect;
        // RGB color history is written by the first wavelet iteration, not by reprojection.
        rhi::ITexture* lightingHistory;
    };

    Output compile(
        rhi::IDevice&                                          device,
        ShaderManager&                                         shaders,
        const SVGFInputs&                                      source,
        const std::array<rhi::ITexture*, kHistoryCount>&       history,
        const std::array<rhi::ITexture*, kLightingLayerCount>& outputs
    );
    void execute(rhi::ICommandEncoder& commands, const GpuScene& scene, uint64_t historyRevision);

    // Layer belonging to the frame most recently executed by this pass.
    [[nodiscard]] uint32_t colorHistoryLayer() const { return writeLayer ^ 1u; }

private:
    rhi::Extent3D                      extent{};
    rhi::ComPtr<rhi::IComputePipeline> pipeline;
    rhi::ComPtr<rhi::IShaderObject>    shaderObject;
    glm::mat4                          previousViewProjection{1.0f};
    glm::vec4                          previousDepthRow{};
    uint32_t                           writeLayer = 0;
    bool                               hasHistory = false;
    const SvgfSettings&                settings;
    uint64_t                           previousHistoryRevision = 0;
    uint64_t                           previousSceneGeneration  = 0;

    void createPipeline(rhi::IDevice& device, ShaderManager& shaders);
};
