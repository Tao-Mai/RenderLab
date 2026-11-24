#include "passes/denoise/SVGF/SVGFFilter.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"
#include "render/GpuScene.h"
#include "render/RenderContext.h"

namespace
{

struct FilterParameters
{
    glm::vec4 viewport;
    uint32_t  stepSize;
    uint32_t  historyLayer;
};
static_assert(sizeof(FilterParameters) == 24);

enum class FilterVariant
{
    First  = 0,
    Filter = 1,
    Final  = 2,
};
}  // namespace

void SVGFFilter::compile(
    const RenderContext&                                   context,
    const SVGFInputs&                                      traced,
    const SVGFReprojection::Output&                        lighting,
    const std::array<rhi::ITexture*, kLightingLayerCount>& scratch,
    rhi::ITexture*                                         color,
    const SVGFReprojection&                                reprojection
)
{
    if (filters.empty())
        for (uint32_t iteration = 0; iteration < kSvgfFilterIterations; ++iteration)
            filters.emplace_back(iteration, reprojection);
    const std::array signals{std::array{lighting.direct, lighting.indirect}, scratch};
    for (uint32_t iteration = 0; iteration < filters.size(); ++iteration)
    {
        const auto& [direct, indirect]             = signals[iteration % signals.size()];
        const auto& [targetDirect, targetIndirect] = signals[(iteration + 1) % signals.size()];
        filters[iteration].compile(
            context,
            {
                .direct          = direct,
                .indirect        = indirect,
                .targetDirect    = targetDirect,
                .targetIndirect  = targetIndirect,
                .color           = color,
                .depth           = traced.depth,
                .normal          = traced.worldNormal,
                .emission        = traced.emission,
                .albedo          = traced.albedo,
                .lightingHistory = lighting.lightingHistory,
            }
        );
    }
}


void SVGFFilter::execute(const RenderContext& frame) const
{
    for (const auto& filter : filters)
        filter.execute(frame);
}


void SVGFFilter::FilterPass::compile(const RenderContext& context, const FilterResources& resources)
{
    extent = resources.direct->getDesc().size;
    if (!pipeline)
        createPipeline(context);
    rhiCheck(context.device->createRootShaderObject(pipeline->getProgram(), shaderObject.writeRef()));
    const rhi::ShaderCursor root(shaderObject);
    rhiCheck(root["inputDirect"].setBinding(resources.direct));
    rhiCheck(root["inputIndirect"].setBinding(resources.indirect));
    rhiCheck(root["depthImage"].setBinding(resources.depth));
    rhiCheck(root["normalImage"].setBinding(resources.normal));
    if (last())
    {
        rhiCheck(root["outputColor"].setBinding(resources.color));
        rhiCheck(root["emission"].setBinding(resources.emission));
        rhiCheck(root["albedoImage"].setBinding(resources.albedo));
    }
    else
    {
        rhiCheck(root["outputDirect"].setBinding(resources.targetDirect));
        rhiCheck(root["outputIndirect"].setBinding(resources.targetIndirect));
        if (first())
            rhiCheck(root["historyLighting"].setBinding(resources.lightingHistory));
    }
}


void SVGFFilter::FilterPass::createPipeline(const RenderContext& context)
{
    const auto variant = first() ? FilterVariant::First : last() ? FilterVariant::Final : FilterVariant::Filter;
    pipeline           = context.device->createComputePipeline(
        {.program = context.shaderManager->getOrLoad("svgf_filter", {{"SVGF_STAGE", std::to_string(int(variant))}})}
    );
    CHECK(pipeline);
}


void SVGFFilter::FilterPass::execute(const RenderContext& frame) const
{
    const auto& viewport = frame.scene->viewport();
    {
        const auto uniformValue = FilterParameters{
            .viewport     = {viewport.x, viewport.y, viewport.width, viewport.height},
            .stepSize     = 1u << iteration,
            .historyLayer = reprojection.colorHistoryLayer(),
        };
        const auto uniformCursor = rhi::ShaderCursor(shaderObject)["parameters"].getDereferenced();
        CHECK(uniformCursor.getTypeLayout()->getSize() <= sizeof(uniformValue));
        rhiCheck(uniformCursor.setData(&uniformValue, uniformCursor.getTypeLayout()->getSize()));
    };
    auto* pass = frame.commands->beginComputePass();
    CHECK(pass);
    pass->bindPipeline(pipeline, shaderObject);
    const auto groups = dispatchGroups(pipeline, {extent.width, extent.height, 1});
    pass->dispatchCompute(groups.width, groups.height, groups.depth);
    pass->end();
}
