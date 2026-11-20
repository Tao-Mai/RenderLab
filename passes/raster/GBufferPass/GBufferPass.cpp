#include "passes/raster/GBufferPass/GBufferPass.h"
#include "render/plugins/PluginManager.h"

#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/device/VulkanContext.h"
#include "render/resources/MaterialTable.h"

namespace
{
constexpr std::string_view kGBufferShader = "gbuffer";
}

RenderPassReflection GBufferPass::reflect(const PassSetup&) const
{
    RenderPassReflection reflection;
    reflection.input("depth", PortKind::Image, false, vk::Format::eD32Sfloat);
    reflection.output("diffuseOcclusion", PortKind::Image, false, vk::Format::eR8G8B8A8Srgb);
    reflection.output("normalRoughness", PortKind::Image, false, vk::Format::eR16G16B16A16Sfloat);
    reflection.output("emissiveSpecularF90", PortKind::Image, false, vk::Format::eR16G16B16A16Sfloat);
    reflection.output("specularF0DiffuseFresnel", PortKind::Image, false, vk::Format::eR16G16B16A16Sfloat);
    reflection.output("sheenColorRoughness", PortKind::Image, false, vk::Format::eR16G16B16A16Sfloat);
    return reflection;
}

void GBufferPass::compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports)
{
    const auto depthHandle = ports.image("depth");

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
    ports.setOutput("diffuseOcclusion", colors[0]);
    ports.setOutput("normalRoughness", colors[1]);
    ports.setOutput("emissiveSpecularF90", colors[2]);
    ports.setOutput("specularF0DiffuseFresnel", colors[3]);
    ports.setOutput("sheenColorRoughness", colors[4]);
}

void GBufferPass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution)
{
    attachments.begin(graph, commandBuffer, execution.view.viewport);
    bindMeshGeometry(commandBuffer, execution);
    MeshDrawRecorder recorder{commandBuffer, execution};
    recorder.drawOpaque([&](const auto& material) { return pipelines.get(material); });
    commandBuffer.endRendering();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<GBufferPass>();
}
