#include "render/feature/ScenePass.h"

#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RendererConfig.h"
#include "render/device/VulkanContext.h"
#include "render/feature/SceneDraw.h"
#include "render/resource/MaterialTable.h"

#include <array>

ScenePass::Output ScenePass::setup(
    RenderGraph&              graph,
    const PassSetup&          setup,
    RenderGraph::ImageHandle  target,
    RenderGraph::ImageHandle  depthHandle,
    ShadowPass::Output        shadow,
    RenderGraph::BufferHandle lights
)
{
    renderContext().materialTable->use(graph, vk::PipelineStageFlagBits2::eFragmentShader);
    const auto& config = setup.config;
    shaderOptions      = {.ibl = config.feature.ibl, .clearcoat = config.renderPath != RenderPath::Deferred};

    const auto resolvedShadow = SceneDraw::useLightingResources(graph, lights, shadow);
    const bool hdr            = config.feature.hdr;
    if (hdr)
    {
        const auto features =
            setup.vulkan.physicalDeviceHandle().getFormatProperties(kHdrColorFormat).optimalTilingFeatures;
        constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eColorAttachment |
            vk::FormatFeatureFlagBits::eColorAttachmentBlend | vk::FormatFeatureFlagBits::eSampledImage |
            vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
        CHECK(
            (features & kRequiredFeatures) == kRequiredFeatures,
            "GPU does not support an RGBA16F HDR color attachment with blending and sampling"
        );
    }

    const auto extent = graph.imageExtent(depthHandle);
    color = hdr ? graph.createImage({.format = kHdrColorFormat, .extent = {extent.width, extent.height, 1}}) : target;
    color = graph.write(color);
    depth = depthHandle;
    SceneDraw::useAttachments(graph, std::array{color}, depth);
    return {color, depth, resolvedShadow};
}

void ScenePass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
{
    SceneDraw::beginRendering(graph, commandBuffer, execution, std::array{color}, depth, vk::AttachmentLoadOp::eClear);
    SceneDraw::recordMeshes(commandBuffer, execution, std::array{graph.imageInfo(color).format}, {}, shaderOptions);
    commandBuffer.endRendering();
}
