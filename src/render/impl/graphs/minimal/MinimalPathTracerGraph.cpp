#include "render/impl/graphs/minimal/MinimalPathTracerGraph.h"

#include "render/graph/RenderGraph.h"
#include "render/impl/passes/accumulate/AccumulatePass.h"
#include "render/impl/passes/debug/DebugPass.h"
#include "render/impl/passes/minimal/MinimalPathTracer.h"
#include "render/impl/passes/vbuffer/VBufferPass.h"
#include "render/impl/postprocess/ExposurePass/ExposurePass.h"
#include "render/impl/postprocess/FalcorPostProcess/FalcorPostProcess.h"
#include "render/impl/postprocess/ToneMappingPass/ToneMappingPass.h"
#include "render/impl/postprocess/bloom/Bloom.h"

#include <string>

std::shared_ptr<RenderGraph> buildMinimalPathTracerGraph(
    rhi::ComPtr<rhi::IDevice> device, const RenderGraphSettings& settings
)
{
    auto graph = std::make_shared<RenderGraph>(device, std::string{MinimalPathTracer::kType});
    graph->addPass(std::make_shared<VBufferPass>(device), "vbuffer");
    graph->addPass(std::make_shared<MinimalPathTracer>(device), "pathTracer");
    graph->addEdge("vbuffer.hit", "pathTracer.vbuffer");

    std::string color = "pathTracer.color";
    if (settings.accumulate)
    {
        graph->addPass(std::make_shared<AccumulatePass>(device), "accumulate");
        graph->addEdge(color, "accumulate.source");
        color = "accumulate.color";
    }

    if (settings.hdr)
    {
        if (settings.postProcess == PostProcessPipeline::Falcor)
        {
            graph->addPass(std::make_shared<FalcorPostProcess>(device), "falcor");
            graph->addEdge(color, "falcor.source");
            color = "falcor.color";
        }
        else
        {
            graph->addPass(std::make_shared<ExposurePass>(device), "exposure");
            graph->addEdge(color, "exposure.hdrColor");

            if (settings.bloom)
            {
                graph->addPass(std::make_shared<Bloom>(device), "bloom");
                graph->addEdge(color, "bloom.hdrColor");
                color = "bloom.color";
            }

            graph->addPass(std::make_shared<ToneMappingPass>(device), "toneMapping");
            graph->addEdge(color, "toneMapping.hdrColor");
            graph->addEdge("exposure.state", "toneMapping.exposure");
            color = "toneMapping.color";
        }
    }

    graph->addPass(std::make_shared<DebugPass>(device), "debug");
    graph->addEdge(color, "debug.source");
    graph->addEdge("vbuffer.hit", "debug.vbuffer");
    graph->markOutput("debug.color");

    return graph;
}
