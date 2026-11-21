#pragma once

#include "render/device/ResourceBarriers.h"
#include "passes/denoise/SVGF/SvgfSettings.h"

#include "render/device/Device.h"
#include "render/denoise/DenoiserInputs.h"

#include <array>

namespace Falcor
{
class RenderContext;
}  // namespace Falcor

class SVGFReprojection
{
    static constexpr size_t kHistoryCount = 4;

public:
    explicit SVGFReprojection(const SvgfSettings& settings) : settings(settings) {}

    struct Output
    {
        // RGBA32F: RGB accumulated albedo-demodulated color, A its estimated luminance variance.
        VulkanResources::ImageHandle direct;
        VulkanResources::ImageHandle indirect;
        // RGB color history is written by the first wavelet iteration, not by reprojection.
        VulkanResources::ImageHandle lightingHistory;
    };

    Output compile(
        const Falcor::RenderContext& context,
        const DenoiserInputs& source,
        const std::array<VulkanResources::ImageHandle, kHistoryCount>& history,
        const std::array<VulkanResources::ImageHandle, kLightingLayerCount>& outputs
    );
    void execute(VulkanResources& cache, vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame);

    // Layer belonging to the frame most recently executed by this pass.
    [[nodiscard]] uint32_t colorHistoryLayer() const { return writeLayer ^ 1u; }

private:
    VulkanResources::ImageHandle              historySource;
    ResourceBarriers                    accesses;
    vk::Extent3D                            extent{};
    DescriptorSetLayout                     descriptorSetLayout;
    vk::PipelineLayout                      pipelineLayout;
    ComputePipeline                         pipeline;
    vk::raii::DescriptorSet                 descriptorSet = nullptr;
    glm::mat4                               previousViewProjection{1.0f};
    glm::vec4                               previousViewport{};
    glm::vec4                               previousDepthRow{};
    uint32_t                                writeLayer         = 0;
    bool                                    hasHistory         = false;
    const SvgfSettings&                    settings;
    uint64_t                                previousHistoryRevision = 0;

    void createPipeline(const Falcor::RenderContext& context);
};
