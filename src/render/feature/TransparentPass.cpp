#include "render/feature/TransparentPass.h"

#include "render/feature/SceneDraw.h"
#include "render/RenderData.h"

#include <array>

TransparentPass::Output TransparentPass::setup(
    RenderGraph&             graph,
    RenderGraph::ImageHandle colorHandle,
    RenderGraph::ImageHandle depthHandle,
    ShadowPass::Output       shadow
)
{
    color = graph.write(colorHandle);
    depth = depthHandle;
    SceneDraw::useAttachments(graph, std::array{color}, depth);
    for (const auto image : {shadow.pointLightShadowMap, shadow.rectAreaLightShadowMap, shadow.cascadeShadowMap})
    {
        graph.use(
            image,
            {
                .stages = vk::PipelineStageFlagBits2::eFragmentShader,
                .access = vk::AccessFlagBits2::eShaderSampledRead,
                .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
            }
        );
    }
    return {color};
}

void TransparentPass::execute(RenderGraph& graph) const
{
    const auto& items = graph.renderData().transparentDrawItems;
    if (items.empty())
        return;

    SceneDraw::beginRendering(graph, std::array{color}, depth, vk::AttachmentLoadOp::eLoad);
    SceneDraw::recordMeshes(graph, items, std::array{graph.imageInfo(color).format});
    graph.commands().endRendering();
}
