#include "passes/raster/GBufferPass/GBufferPass.h"

#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/device/VulkanContext.h"
#include "render/resources/MaterialTable.h"

namespace
{
constexpr std::string_view kGBufferShader = "gbuffer";
}

GBufferPass::Output GBufferPass::setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle depthHandle)
{
    renderContext().materialTable->use(graph, vk::PipelineStageFlagBits2::eFragmentShader);
    const auto& device = setup.vulkan.physicalDeviceHandle();
    CHECK(kColorFormats.size() <= setup.vulkan.properties().limits.maxColorAttachments);
    constexpr auto kRequiredFeatures =
        vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage;
    const auto extent = graph.imageExtent(depthHandle);
    std::array<RenderGraph::ImageHandle, kColorFormats.size()> colors;
    for (size_t index = 0; index < colors.size(); ++index)
    {
        const auto features = device.getFormatProperties(kColorFormats[index]).optimalTilingFeatures;
        CHECK(
            (features & kRequiredFeatures) == kRequiredFeatures,
            "GPU does not support G-buffer format {}",
            vk::to_string(kColorFormats[index])
        );
        colors[index] = graph.write(graph.createImage({
            .format = kColorFormats[index],
            .extent = {extent.width, extent.height, 1},
        }));
        attachments.colors[index] = {colors[index]};
    }
    CHECK(
        device.getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eSampledImage
    );
    attachments.depth = RasterDepthAttachment{depthHandle};
    attachments.declare(graph);

    auto& ctx = renderContext();
    pipelines = createMeshCullPipelines(
        *ctx.device,
        {
            .shader       = ctx.shaderManager->getOrLoad(kGBufferShader),
            .layout       = ctx.device->meshPipelineLayout,
            .state        = {.depthCompare = vk::CompareOp::eEqual, .depthWrite = false},
            .colorFormats = {kColorFormats.begin(), kColorFormats.end()},
            .depthFormat  = vk::Format::eD32Sfloat,
        }
    );
    return {colors[0], colors[1], colors[2], colors[3], colors[4], depthHandle};
}

void GBufferPass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
{
    attachments.begin(graph, commandBuffer, execution.view.viewport);
    bindMeshGeometry(commandBuffer, execution);
    MeshDrawRecorder recorder{commandBuffer, execution};
    recorder.drawOpaque([&](const auto& material) { return pipelines.get(material); });
    commandBuffer.endRendering();
}
