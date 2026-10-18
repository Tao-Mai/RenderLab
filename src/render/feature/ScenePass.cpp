#include "render/feature/ScenePass.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/RenderData.h"
#include "render/device/VulkanContext.h"
#include "render/feature/SceneDraw.h"
#include "render/present/Swapchain.h"

#include <array>

ScenePass::Output ScenePass::setup(
    RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle depthHandle, ShadowPass::Output shadow
)
{
    const auto& config = context().config->rendererConfig();
    shaderOptions      = {.ibl = config.feature.ibl, .clearcoat = config.renderPath != RenderPath::Deferred};

    const auto resolvedShadow = bindings.setup(graph, setup, shadow);
    const bool hdr            = context().config->rendererConfig().feature.hdr;
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

    const auto extent = setup.swapchain.extent();
    color             = hdr ? graph.createImage({.format = kHdrColorFormat, .extent = {extent.width, extent.height, 1}})
                            : graph.importSwapchain();
    color             = graph.write(color);
    depth             = depthHandle;
    SceneDraw::useAttachments(graph, std::array{color}, depth);
    return {color, depth, resolvedShadow};
}

void ScenePass::prepare(RenderGraph& graph, const PassSetup& setup) const { bindings.prepare(graph, setup); }

void ScenePass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
{
    SceneDraw::beginRendering(graph, commandBuffer, execution, std::array{color}, depth, vk::AttachmentLoadOp::eClear);
    SceneDraw::recordMeshes(
        commandBuffer,
        execution,
        execution.renderData.opaqueDrawItems,
        std::array{graph.imageInfo(color).format},
        {},
        shaderOptions
    );
    commandBuffer.endRendering();
}
