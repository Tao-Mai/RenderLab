#include "passes/denoise/SVGF/SVGFFilter.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"

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
    const Falcor::RenderContext&                                      context,
    const DenoiserInputs&                                             traced,
    const SVGFReprojection::Output&                                   lighting,
    const std::array<GpuResources::ImageHandle, kLightingLayerCount>& scratch,
    GpuResources::ImageHandle                                         color,
    const SVGFReprojection&                                           reprojection
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


void SVGFFilter::execute(GpuResources& cache, rhi::ICommandEncoder& commands, const Falcor::RenderContext& frame) const
{
    for (const auto& filter : filters)
        filter.execute(cache, commands, frame);
}


void SVGFFilter::FilterPass::compile(const Falcor::RenderContext& context, const FilterResources& resources)
{
    auto& cache = *context.gpuResources;
    extent      = cache.imageInfo(resources.direct).size;
    if (!pipeline)
        createPipeline(context);
    rhiCheck(context.device->createRootShaderObject(pipeline->getProgram(), shaderObject.writeRef()));
    rhiCheck(
        rhi::ShaderCursor(shaderObject)["inputDirect"].setBinding(cache.getImage(resources.direct)->getDefaultView())
    );
    rhiCheck(
        rhi::ShaderCursor(shaderObject)["inputIndirect"].setBinding(
            cache.getImage(resources.indirect)->getDefaultView()
        )
    );
    rhiCheck(
        rhi::ShaderCursor(shaderObject)["depthImage"].setBinding(cache.getImage(resources.depth)->getDefaultView())
    );
    rhiCheck(
        rhi::ShaderCursor(shaderObject)["normalImage"].setBinding(cache.getImage(resources.normal)->getDefaultView())
    );
    if (last())
    {
        rhiCheck(
            rhi::ShaderCursor(shaderObject)["outputColor"].setBinding(cache.getImage(resources.color)->getDefaultView())
        );
        rhiCheck(
            rhi::ShaderCursor(shaderObject)["emission"].setBinding(cache.getImage(resources.emission)->getDefaultView())
        );
        rhiCheck(
            rhi::ShaderCursor(shaderObject)["albedoImage"].setBinding(
                cache.getImage(resources.albedo)->getDefaultView()
            )
        );
    }
    else
    {
        rhiCheck(
            rhi::ShaderCursor(shaderObject)["outputDirect"].setBinding(
                cache.getImage(resources.targetDirect)->getDefaultView()
            )
        );
        rhiCheck(
            rhi::ShaderCursor(shaderObject)["outputIndirect"].setBinding(
                cache.getImage(resources.targetIndirect)->getDefaultView()
            )
        );
        if (first())
            rhiCheck(
                rhi::ShaderCursor(shaderObject)["historyLighting"].setBinding(
                    cache.getImage(resources.lightingHistory)->getDefaultView()
                )
            );
    }
}


void SVGFFilter::FilterPass::createPipeline(const Falcor::RenderContext& context)
{
    const auto variant = first() ? FilterVariant::First : last() ? FilterVariant::Final : FilterVariant::Filter;
    pipeline           = context.device->createComputePipeline(
        {.program = context.shaderManager->program(
             context.shaderManager->getOrLoad("svgf_filter", {{"SVGF_STAGE", std::to_string(int(variant))}})
         )}
    );
    CHECK(pipeline);
}


void SVGFFilter::FilterPass::execute(
    GpuResources& cache, rhi::ICommandEncoder& commands, const Falcor::RenderContext& frame
) const
{


    const auto& viewport = frame.view->viewport;
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
    auto* pass = commands.beginComputePass();
    CHECK(pass);
    pass->bindPipeline(pipeline, shaderObject);
    const auto groups = dispatchGroups(pipeline, {extent.width, extent.height, 1});
    pass->dispatchCompute(groups.width, groups.height, groups.depth);
    pass->end();
}
