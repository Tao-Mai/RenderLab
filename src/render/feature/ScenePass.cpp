#include "render/feature/ScenePass.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/RenderData.h"
#include "render/device/VulkanContext.h"
#include "render/feature/SceneDraw.h"
#include "render/present/Swapchain.h"

#include <array>

ScenePass::Output ScenePass::setup(RenderGraph& graph, RenderGraph::ImageHandle depthHandle, ShadowPass::Output shadow)
{
    const auto resolvedShadow = bindings.setup(graph, shadow);
    const bool hdr            = context().config->rendererConfig().feature.hdr;
    if (hdr)
    {
        const auto features =
            graph.vulkan().physicalDeviceHandle().getFormatProperties(kHdrColorFormat).optimalTilingFeatures;
        constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eColorAttachment |
            vk::FormatFeatureFlagBits::eColorAttachmentBlend | vk::FormatFeatureFlagBits::eSampledImage |
            vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
        CHECK(
            (features & kRequiredFeatures) == kRequiredFeatures,
            "GPU does not support an RGBA16F HDR color attachment with blending and sampling"
        );
    }

    const auto extent = graph.swapchain().extent();
    color             = hdr ? graph.createImage({.format = kHdrColorFormat, .extent = {extent.width, extent.height, 1}})
                            : graph.importSwapchain();
    color             = graph.write(color);
    depth             = depthHandle;
    SceneDraw::useAttachments(graph, std::array{color}, depth);
    return {color, depth, resolvedShadow};
}

void ScenePass::prepare(RenderGraph& graph) const { bindings.prepare(graph); }

void ScenePass::updateFrame(FrameData& frame, const ViewUniforms& view, const RenderData& scene) const
{
    bindings.updateFrame(frame, view, scene);
}

void ScenePass::execute(RenderGraph& graph) const
{
    SceneDraw::beginRendering(graph, std::array{color}, depth, vk::AttachmentLoadOp::eClear);
    SceneDraw::recordMeshes(graph, graph.renderData().opaqueDrawItems, std::array{graph.imageInfo(color).format});
    graph.commands().endRendering();
}
