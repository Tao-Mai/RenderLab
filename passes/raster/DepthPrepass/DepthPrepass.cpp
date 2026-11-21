#include "render/device/ResourceBarriers.h"
#include "passes/raster/DepthPrepass/DepthPrepass.h"

#include "core/Logger.h"
#include "core/Plugin.h"
#include "render/RenderView.h"
#include "render/device/VulkanContext.h"
#include "render/resources/MaterialTable.h"

Falcor::RenderPassReflection DepthPrepass::reflect(const CompileData&)
{
    Falcor::RenderPassReflection reflection;
    reflection.addOutput("depth", "Scene depth").format(Falcor::ResourceFormat::D32Float).bindFlags(Falcor::ResourceBindFlags::DepthStencil);
    return reflection;
}

void DepthPrepass::compile(Falcor::RenderContext* passContext, const CompileData&)
{
    CHECK(
        passContext->vulkan->physicalDeviceHandle().getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );

    auto& device = *passContext->getDevice();
    pipelines = createMeshCullPipelines(
        device,
        {
            .shader      = passContext->shaderManager->getOrLoad("depth"),
            .layout      = device.meshPipelineLayout,
            .state       = {.vertexLayout = VertexLayoutPreset::PositionUv},
            .depthFormat = vk::Format::eD32Sfloat,
        }
    );

    barriers.clear();
}

void DepthPrepass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache         = *renderCommands->vulkanResources;
    auto&       commandBuffer = *renderCommands->commands;
    const auto& execution     = *renderCommands;
    if (barriers.empty())
    {
        const auto depth = cache.registerImage(renderData.getTexture("depth"));
        attachments.depth = RasterDepthAttachment{.image = depth, .readOnly = false};
        barriers.add(depth, vulkan_access::kDepthWrite);
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
    registry.registerClass<Falcor::RenderPass, DepthPrepass>();
}
