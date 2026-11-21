#include "render/device/ResourceBarriers.h"
#include "passes/raster/ScenePass/ScenePass.h"

#include "core/Logger.h"
#include "core/Plugin.h"

#include "render/RenderContext.h"
#include "render/PreparedScene.h"
#include "render/RendererConfig.h"
#include "render/RenderView.h"
#include "render/device/VulkanContext.h"
#include "render/resources/NativeFormats.h"
#include "render/raster/MeshDraw.h"
#include "render/resources/MaterialTable.h"

ScenePass::ScenePass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<RasterLightingSettings>(properties))
{
}

Falcor::Properties ScenePass::getProperties() const
{
    return Falcor::serializeToProperties(parameters);
}

void ScenePass::setProperties(const Falcor::Properties& properties)
{
    RasterLightingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);

    const bool recompile = next.hdr != parameters.hdr;
    parameters = std::move(next);
    if (recompile)
        requestRecompile();
}

Falcor::RenderPassReflection ScenePass::reflect(const CompileData& compileData)
{
    const auto* depth = compileData.connectedResources.getField("depth");
    const auto extent = depth ? Falcor::uint2{depth->getWidth(), depth->getHeight()} : Falcor::uint2{};

    Falcor::RenderPassReflection reflection;
    reflection.addInput("depth", "Scene depth").format(Falcor::ResourceFormat::D32Float).bindFlags(Falcor::ResourceBindFlags::DepthStencil);
    reflection.addOutput("color", "Raster lighting result")
        .format(parameters.getColorFormat(compileData.defaultTexFormat))
        .bindFlags(Falcor::ResourceBindFlags::RenderTarget)
        .texture2D(extent.x, extent.y);
    return reflection;
}

void ScenePass::compile(Falcor::RenderContext* passContext, const CompileData& compileData)
{
    const auto& config = *passContext->config;
    parameters.validateColorFormat(*passContext->vulkan);

    pipelines.setup(
        *passContext->getDevice(),
        Falcor::getVulkanFormat(parameters.getColorFormat(compileData.defaultTexFormat)),
        {.depthCompare = vk::CompareOp::eEqual, .depthWrite = false},
        config.feature.ibl,
        config.renderPath != RenderPath::Deferred
    );

    barriers.clear();
}

void ScenePass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache         = *renderCommands->vulkanResources;
    auto&       commandBuffer = *renderCommands->commands;
    const auto& execution     = *renderCommands;

    if (barriers.empty())
    {
        const auto depthHandle = cache.registerImage(renderData.getTexture("depth"));
        attachments.depth = RasterDepthAttachment{depthHandle};

        constexpr auto kFragment = vk::PipelineStageFlagBits2::eFragmentShader;
        barriers.add(depthHandle, vulkan_access::kDepthRead);
        barriers.add(execution.frameData->lightBufferHandle(), vulkan_access::storageRead(kFragment));
        barriers.add(execution.materialTable->bufferHandle(), vulkan_access::storageRead(kFragment));
        barriers.declare(cache);
    }

    barriers.transition(cache, commandBuffer);
    const auto color = cache.registerImage(renderData.getTexture("color"));
    attachments.colors.front() = {color, vk::AttachmentLoadOp::eClear};
    cache.transition(commandBuffer, color, vulkan_access::kColorWrite);

    attachments.begin(cache, commandBuffer, execution.view->viewport);
    bindForwardPbrResources(commandBuffer, execution);
    MeshDrawRecorder recorder{commandBuffer, execution};
    recorder.drawOpaque([&](const auto& material) { return pipelines.get(execution, material); });
    commandBuffer.endRendering();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, ScenePass>();
}
