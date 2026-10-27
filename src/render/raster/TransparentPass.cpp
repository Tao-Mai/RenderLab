#include "render/raster/TransparentPass.h"
#include "core/Context.h"
#include "render/RendererConfig.h"

#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/raster/SceneDraw.h"
#include "render/resources/MaterialTable.h"

#include <algorithm>
#include <array>
#include <glm/geometric.hpp>

TransparentPass::Output TransparentPass::setup(
    RenderGraph&              graph,
    const PassSetup&          setup,
    RenderGraph::ImageHandle  colorHandle,
    RenderGraph::ImageHandle  depthHandle,
    ShadowPass::Output        shadow,
    RenderGraph::BufferHandle lights
)
{
    renderContext().materialTable->use(graph, vk::PipelineStageFlagBits2::eFragmentShader);
    const auto& config = setup.config;
    shaderOptions =
        {.ibl = config.feature.ibl, .clearcoat = config.renderPath != RenderPath::Deferred, .transparent = true};

    color = graph.write(colorHandle);
    depth = depthHandle;
    SceneDraw::useAttachments(graph, std::array{color}, depth);
    SceneDraw::useLightingResources(graph, lights, shadow);
    return {color};
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
) const
{
    if (sortedDrawItems.empty())
        return;

    SceneDraw::beginRendering(graph, commandBuffer, execution, std::array{color}, depth, vk::AttachmentLoadOp::eLoad);
    SceneDraw::recordMeshes(
        commandBuffer,
        execution,
        std::array{graph.imageInfo(color).format},
        {},
        shaderOptions,
        sortedDrawItems
    );
    commandBuffer.endRendering();
}
