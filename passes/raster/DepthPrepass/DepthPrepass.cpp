#include "passes/raster/DepthPrepass/DepthPrepass.h"
#include "render/RenderView.h"
#include "render/device/VulkanContext.h"

DepthPrepass::Output DepthPrepass::setup(RenderGraph& graph, const PassSetup& setup)
{
    renderContext().materialTable->use(graph, vk::PipelineStageFlagBits2::eFragmentShader);
    CHECK(
        setup.vulkan.physicalDeviceHandle().getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );

    const auto extent = setup.renderExtent;
    const auto depth = graph.write(
        graph.createImage({.format = vk::Format::eD32Sfloat, .extent = {extent.width, extent.height, 1}})
    );
    attachments.depth = RasterDepthAttachment{.image = depth, .readOnly = false};
    attachments.declare(graph);

    auto& ctx = renderContext();
    pipelines = createMeshCullPipelines(
        *ctx.device,
        {
            .shader      = ctx.shaderManager->getOrLoad("depth"),
            .layout      = ctx.device->meshPipelineLayout,
            .state       = {.vertexLayout = VertexLayoutPreset::PositionUv},
            .depthFormat = vk::Format::eD32Sfloat,
        }
    );
    return {depth};
}

void DepthPrepass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
{
    attachments.begin(graph, commandBuffer, execution.view.viewport);
    bindMeshGeometry(commandBuffer, execution);
    MeshDrawRecorder recorder{commandBuffer, execution};
    recorder.drawOpaque([&](const auto& material) { return pipelines.get(material); });
    commandBuffer.endRendering();
}
