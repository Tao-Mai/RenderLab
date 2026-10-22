#include "render/feature/TransparentPass.h"
#include "core/ConfigManager.h"
#include "core/Context.h"

#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/feature/SceneDraw.h"
#include "render/resource/RenderResourceManager.h"

#include <algorithm>
#include <array>
#include <glm/geometric.hpp>

TransparentPass::Output TransparentPass::setup(
    RenderGraph&             graph,
    const PassSetup&         setup,
    RenderGraph::ImageHandle colorHandle,
    RenderGraph::ImageHandle depthHandle,
    ShadowPass::Output       shadow
)
{
    const auto& config = context().config->rendererConfig();
    shaderOptions =
        {.ibl = config.feature.ibl, .clearcoat = config.renderPath != RenderPath::Deferred, .transparent = true};

    color = graph.write(colorHandle);
    depth = depthHandle;
    SceneDraw::useAttachments(graph, std::array{color}, depth);
    for (const auto image :
         {shadow.pointLightShadowMap,
          shadow.spotLightShadowMap,
          shadow.rectAreaLightShadowMap,
          shadow.cascadeShadowMap})
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

void TransparentPass::prepare(RenderGraph&, const PassFrame& frame)
{
    CpuProfileScope timing{context().profiler, "Transparent sorting"};
    sortedObjects.clear();
    auto& resources = *renderContext().resources;
    for (uint32_t objectIndex = 0; objectIndex < frame.renderData.objects.size(); ++objectIndex)
    {
        if (!frame.renderData.isVisible(objectIndex))
            continue;
        const auto& object = frame.renderData.objects[objectIndex];
        const auto& mesh   = resources.mesh(object.mesh);
        for (uint32_t index = 0; index < mesh.submeshes().size(); ++index)
        {
            const auto& binding = frame.renderData.material(object, index);
            if (binding.material->alphaMode() == MaterialAlphaMode::Blend)
            {
                sortedObjects.push_back(objectIndex);
                break;
            }
        }
    }
    std::ranges::sort(
        sortedObjects,
        std::greater{},
        [&](uint32_t objectIndex)
        {
            const auto offset = glm::vec3{frame.renderData.objects[objectIndex].model[3]} -
                glm::vec3{frame.view.uniforms.cameraPosition};
            return glm::dot(offset, offset);
        }
    );
}

void TransparentPass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution
) const
{
    if (sortedObjects.empty())
        return;

    SceneDraw::beginRendering(graph, commandBuffer, execution, std::array{color}, depth, vk::AttachmentLoadOp::eLoad);
    SceneDraw::recordMeshes(
        commandBuffer,
        execution,
        std::array{graph.imageInfo(color).format},
        {},
        shaderOptions,
        sortedObjects
    );
    commandBuffer.endRendering();
}
