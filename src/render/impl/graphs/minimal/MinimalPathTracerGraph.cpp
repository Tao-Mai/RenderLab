#include "render/impl/graphs/minimal/MinimalPathTracerGraph.h"

#include "render/graph/RenderGraph.h"
#include "render/impl/postprocess/ExposurePass/ExposurePass.h"
#include "render/impl/postprocess/FalcorPostProcess/FalcorPostProcess.h"
#include "render/impl/postprocess/ToneMappingPass/ToneMappingPass.h"
#include "render/impl/postprocess/bloom/Bloom.h"
#include "render/impl/passes/MinimalPathTracer.h"

#include <string>

std::shared_ptr<RenderGraph> buildMinimalPathTracerGraph(
    rhi::ComPtr<rhi::IDevice> device, const RenderGraphSettings& settings
)
{
    const std::string kPathTracerColor = "pathTracer.color";
    auto graph = std::make_shared<RenderGraph>(device, std::string{MinimalPathTracer::kType});
    graph->addPass(std::make_shared<MinimalPathTracer>(device), "pathTracer");

    if (!settings.hdr)
    {
        graph->markOutput(kPathTracerColor);
        return graph;
    }

    if (settings.postProcess == PostProcessPipeline::Falcor)
    {
        graph->addPass(std::make_shared<FalcorPostProcess>(device), "falcor");
        graph->addEdge(kPathTracerColor, "falcor.source");
        graph->markOutput("falcor.color");
        return graph;
    }

    graph->addPass(std::make_shared<ExposurePass>(device), "exposure");
    graph->addEdge(kPathTracerColor, "exposure.hdrColor");

    auto color = kPathTracerColor;
    if (settings.bloom)
    {
        graph->addPass(std::make_shared<Bloom>(device), "bloom");
        graph->addEdge(kPathTracerColor, "bloom.hdrColor");
        color = "bloom.color";
    }

    graph->addPass(std::make_shared<ToneMappingPass>(device), "toneMapping");
    graph->addEdge(color, "toneMapping.hdrColor");
    graph->addEdge("exposure.state", "toneMapping.exposure");
    graph->markOutput("toneMapping.color");
    return graph;
}
