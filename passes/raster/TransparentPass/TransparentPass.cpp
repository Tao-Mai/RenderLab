#include "passes/raster/TransparentPass/TransparentPass.h"
#include "render/plugins/PluginManager.h"
#include "render/graph/PassPorts.h"
#include "core/Context.h"
#include "render/RendererConfig.h"

#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/raster/MeshDraw.h"
#include "render/resources/MaterialTable.h"

#include <algorithm>
#include <glm/geometric.hpp>

RenderPassReflection TransparentPass::reflect(const PassSetup&) const
{
    RenderPassReflection reflection;
    reflection.input("color", PortKind::Image);
    reflection.input("depth", PortKind::Image, false, vk::Format::eD32Sfloat);
    reflection.input("lights", PortKind::Buffer);
    pass_ports::inputs<ShadowResources>(reflection);
    reflection.output("color", PortKind::Image);
    return reflection;
}

void TransparentPass::compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports)
{
    const auto colorHandle = ports.image("color");
    const auto depthHandle = ports.image("depth");
    const auto lights = ports.buffer("lights");
    const auto shadow = pass_ports::read<ShadowResources>(ports);

    renderContext().materialTable->use(graph, vk::PipelineStageFlagBits2::eFragmentShader);
    useRasterLighting(graph, lights, shadow);

    const auto color      = graph.write(colorHandle);
    attachments.colors[0] = {color, vk::AttachmentLoadOp::eLoad};
    attachments.depth     = RasterDepthAttachment{depthHandle};
    attachments.declare(graph);

    const auto& config = setup.config;
    pipelines.setup(
        graph.imageInfo(color).format, {.depthCompare = vk::CompareOp::eLess, .depthWrite = false, .blend = true},
        config.feature.ibl, config.renderPath != RenderPath::Deferred
    );
    ports.setOutput("color", color);
}

void TransparentPass::prepare(RenderGraph&, const PassFrame& frame)
{
    CpuProfileScope timing{context().profiler, "Transparent sorting"};
    sortedDrawItems.clear();
    const auto& materials = *renderContext().materialTable;
    for (uint32_t itemIndex = 0; itemIndex < frame.renderData.drawItems.size(); ++itemIndex)
    {
        if (!frame.renderData.isVisible(itemIndex))
            continue;
        const auto materialIndex = frame.renderData.materialIndices[itemIndex];
        if (materials.info(materialIndex).alphaMode == kAlphaModeBlend)
            sortedDrawItems.push_back(itemIndex);
    }
    std::ranges::stable_sort(
        sortedDrawItems,
        std::greater{},
        [&](uint32_t itemIndex)
        {
            const auto offset = glm::vec3{frame.renderData.drawItems[itemIndex].model[3]} -
                glm::vec3{frame.view.uniforms.cameraPosition};
            return glm::dot(offset, offset);
        }
    );
}

void TransparentPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution
)
{
    if (sortedDrawItems.empty())
        return;

    attachments.begin(graph, commandBuffer, execution.view.viewport);
    bindForwardPbrResources(commandBuffer, execution);
    MeshDrawRecorder recorder{commandBuffer, execution};
    recorder.drawOrdered(sortedDrawItems, [&](const auto& material) { return pipelines.get(material); });
    commandBuffer.endRendering();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<TransparentPass>();
}
