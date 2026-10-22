#include "render/feature/deferred/GBufferPass.h"

#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/device/VulkanContext.h"
#include "render/feature/SceneDraw.h"
#include "render/present/Swapchain.h"

namespace
{
const ShaderAsset::ID kGBufferShader = "gbuffer";
}

GBufferPass::Output GBufferPass::setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle depthHandle)
{
    const auto& device = setup.vulkan.physicalDeviceHandle();
    CHECK(kColorFormats.size() <= setup.vulkan.properties().limits.maxColorAttachments);
    constexpr auto kRequiredFeatures =
        vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage;
    const auto extent = setup.swapchain.extent();
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
    }
    CHECK(
        device.getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eSampledImage
    );
    depth = depthHandle;
    SceneDraw::useAttachments(graph, colors, depth);
    shader = renderContext().shaderManager->getOrLoad(kGBufferShader);
    return {colors[0], colors[1], colors[2], colors[3], depth};
}

void GBufferPass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
{
    SceneDraw::beginRendering(graph, commandBuffer, execution, colors, depth, vk::AttachmentLoadOp::eClear);
    SceneDraw::recordMeshes(commandBuffer, execution, kColorFormats, shader);
    commandBuffer.endRendering();
}
