#include "render/device/ResourceBarriers.h"
#include "passes/raster/GBufferPass/GBufferPass.h"

#include "render/resources/NativeFormats.h"
#include "core/Logger.h"
#include "core/Plugin.h"

#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/device/VulkanContext.h"
#include "render/resources/MaterialTable.h"

namespace
{
constexpr std::string_view kGBufferShader = "gbuffer";
}

Falcor::RenderPassReflection GBufferPass::reflect(const CompileData& compileData)
{
    const auto* input = compileData.connectedResources.getField("depth");
    const auto extent = input ? Falcor::uint2{input->getWidth(), input->getHeight()} : Falcor::uint2{};

    Falcor::RenderPassReflection reflection;
    reflection.addInput("depth", "Scene depth").format(Falcor::ResourceFormat::D32Float).bindFlags(Falcor::ResourceBindFlags::DepthStencil);
    for (size_t index = 0; index < kColorNames.size(); ++index)
        reflection.addOutput(std::string{kColorNames[index]}, "G-buffer channel")
            .format(Falcor::getResourceFormat(kColorFormats[index]))
            .bindFlags(Falcor::ResourceBindFlags::RenderTarget)
            .texture2D(extent.x, extent.y);
    return reflection;
}

void GBufferPass::compile(Falcor::RenderContext* passContext, const CompileData&)
{
    const auto& physicalDevice = passContext->vulkan->physicalDeviceHandle();
    CHECK(kColorFormats.size() <= passContext->vulkan->properties().limits.maxColorAttachments);
    constexpr auto kRequiredFeatures =
        vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage;
    for (size_t index = 0; index < kColorNames.size(); ++index)
    {
        const auto features = physicalDevice.getFormatProperties(kColorFormats[index]).optimalTilingFeatures;
        CHECK(
            (features & kRequiredFeatures) == kRequiredFeatures,
            "GPU does not support G-buffer format {}",
            vk::to_string(kColorFormats[index])
        );
    }
    CHECK(
        physicalDevice.getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eSampledImage
    );

    auto& device = *passContext->getDevice();
    pipelines = createMeshCullPipelines(
        device,
        {
            .shader       = passContext->shaderManager->getOrLoad(kGBufferShader),
            .layout       = device.meshPipelineLayout,
            .state        = {.depthCompare = vk::CompareOp::eEqual, .depthWrite = false},
            .colorFormats = {kColorFormats.begin(), kColorFormats.end()},
            .depthFormat  = vk::Format::eD32Sfloat,
        }
    );

    barriers.clear();
}

void GBufferPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache         = *renderCommands->vulkanResources;
    auto&       commandBuffer = *renderCommands->commands;
    const auto& execution     = *renderCommands;

    if (barriers.empty())
    {
        const auto depthHandle = cache.registerImage(renderData.getTexture("depth"));
        attachments.depth = RasterDepthAttachment{depthHandle};
        for (size_t index = 0; index < kColorNames.size(); ++index)
            attachments.colors[index] = {cache.registerImage(renderData.getTexture(kColorNames[index]))};

        barriers.add(depthHandle, vulkan_access::kDepthRead);
        for (const auto& target : attachments.colors)
            barriers.add(target.image, vulkan_access::kColorWrite);
        barriers.add(execution.materialTable->bufferHandle(), vulkan_access::storageRead(vk::PipelineStageFlagBits2::eFragmentShader));
        barriers.declare(cache);
    }

    barriers.transition(cache, commandBuffer);

    attachments.begin(cache, commandBuffer, execution.view->viewport);
    bindMeshGeometry(commandBuffer, execution);
    MeshDrawRecorder recorder{commandBuffer, execution};
    recorder.drawOpaque([&](const auto& material) { return pipelines.get(material); });
    commandBuffer.endRendering();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, GBufferPass>();
}
