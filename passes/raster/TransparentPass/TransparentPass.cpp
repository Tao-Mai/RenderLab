#include "render/device/ResourceBarriers.h"
#include "passes/raster/TransparentPass/TransparentPass.h"

#include "core/profiling/Profiler.h"
#include "core/Plugin.h"
#include "core/Context.h"
#include "render/RendererConfig.h"

#include "render/RenderContext.h"
#include "render/PreparedScene.h"
#include "render/RenderView.h"
#include "render/resources/NativeFormats.h"
#include "render/raster/MeshDraw.h"
#include "render/resources/MaterialTable.h"

#include <algorithm>
#include <glm/geometric.hpp>

Falcor::RenderPassReflection TransparentPass::reflect(const CompileData& compileData)
{
    Falcor::RenderPassReflection reflection;
    auto& color = reflection.addInputOutput("color", "Color target").bindFlags(Falcor::ResourceBindFlags::RenderTarget);
    if (const auto* input = compileData.connectedResources.getField("color"))
        color.texture2D(input->getWidth(), input->getHeight()).format(input->getFormat());
    reflection.addInput("depth", "Scene depth").format(Falcor::ResourceFormat::D32Float).bindFlags(Falcor::ResourceBindFlags::DepthStencil);
    return reflection;
}

void TransparentPass::compile(Falcor::RenderContext* passContext, const CompileData& compileData)
{
    const auto& config = *passContext->config;
    pipelines.setup(
        *passContext->getDevice(),
        Falcor::getVulkanFormat(compileData.connectedResources.getField("color")->getFormat()),
        {.depthCompare = vk::CompareOp::eLess, .depthWrite = false, .blend = true},
        config.feature.ibl,
        config.renderPath != RenderPath::Deferred
    );

    barriers.clear();
}

void TransparentPass::sortDrawItems(const Falcor::RenderContext& frame)
{
    CpuProfileScope timing{context().profiler, "Transparent sorting"};
    sortedDrawItems.clear();
    const auto& materials = *frame.materialTable;
    for (uint32_t itemIndex = 0; itemIndex < frame.scene->drawItems.size(); ++itemIndex)
    {
        if (!frame.scene->isVisible(itemIndex))
            continue;
        const auto materialIndex = frame.scene->materialIndices[itemIndex];
        if (materials.info(materialIndex).alphaMode == kAlphaModeBlend)
            sortedDrawItems.push_back(itemIndex);
    }
    std::ranges::stable_sort(
        sortedDrawItems,
        std::greater{},
        [&](uint32_t itemIndex)
        {
            const auto offset = glm::vec3{frame.scene->drawItems[itemIndex].model[3]} -
                glm::vec3{frame.view->uniforms.cameraPosition};
            return glm::dot(offset, offset);
        }
    );
}

void TransparentPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
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

    const auto color = cache.registerImage(renderData.getTexture("color"));
    attachments.colors.front() = {color, vk::AttachmentLoadOp::eLoad};
    cache.transition(commandBuffer, color, vulkan_access::kColorReadWrite);
    barriers.transition(cache, commandBuffer);

    sortDrawItems(execution);
    if (sortedDrawItems.empty())
        return;

    attachments.begin(cache, commandBuffer, execution.view->viewport);
    bindForwardPbrResources(commandBuffer, execution);
    MeshDrawRecorder recorder{commandBuffer, execution};
    recorder.drawOrdered(sortedDrawItems, [&](const auto& material) { return pipelines.get(execution, material); });
    commandBuffer.endRendering();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, TransparentPass>();
}
