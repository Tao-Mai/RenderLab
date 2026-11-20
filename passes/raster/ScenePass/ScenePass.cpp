#include "passes/raster/ScenePass/ScenePass.h"

#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RendererConfig.h"
#include "render/RenderView.h"
#include "render/device/VulkanContext.h"
#include "render/raster/MeshDraw.h"
#include "render/resources/MaterialTable.h"

ScenePass::Output ScenePass::setup(
    RenderGraph&              graph,
    const PassSetup&          setup,
    RenderGraph::ImageHandle  target,
    RenderGraph::ImageHandle  depthHandle,
    ShadowResources           shadow,
    RenderGraph::BufferHandle lights
)
{
    renderContext().materialTable->use(graph, vk::PipelineStageFlagBits2::eFragmentShader);
    useRasterLighting(graph, lights, shadow);

    const auto& config = setup.config;
    const bool hdr = config.feature.hdr;
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
    const auto color = graph.write(
        hdr ? graph.createImage({.format = kHdrColorFormat, .extent = {extent.width, extent.height, 1}}) : target
    );
    attachments.colors[0] = {color};
    attachments.depth     = RasterDepthAttachment{depthHandle};
    attachments.declare(graph);

    pipelines.setup(
        graph.imageInfo(color).format, {.depthCompare = vk::CompareOp::eEqual, .depthWrite = false},
        config.feature.ibl, config.renderPath != RenderPath::Deferred
    );
    return {color};
}

void ScenePass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution)
{
    attachments.begin(graph, commandBuffer, execution.view.viewport);
    bindForwardPbrResources(commandBuffer, execution);
    MeshDrawRecorder recorder{commandBuffer, execution};
    recorder.drawOpaque([&](const auto& material) { return pipelines.get(material); });
    commandBuffer.endRendering();
}
