#include "passes/denoise/SVGF/SVGF.h"

#include "editor/PassEditor.h"
#include "render/graph/PassPorts.h"

RenderPassReflection SVGF::reflect(const PassSetup&) const
{
    RenderPassReflection reflection;
    pass_ports::inputs<DenoiserInputs>(reflection);
    reflection.output("color", PortKind::Image);
    return reflection;
}

void SVGF::compile(RenderGraph& graph, const PassSetup& setup, PassResources& ports)
{
    const auto traced = pass_ports::read<DenoiserInputs>(ports);
    const auto lighting = graph.addStage("SVGF reproject", reprojection, setup, traced);
    ports.setOutput("color", filter.compile(graph, setup, traced, lighting, reprojection).color);
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<SVGF>(makePassEditor<SVGF>("SVGF"));
}
