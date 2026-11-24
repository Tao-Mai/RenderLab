#include "passes/raster/DepthPrepass/DepthPrepass.h"

#include <slang-rhi.h>
#include "core/Logger.h"
#include "core/Plugin.h"
#include "render/RenderView.h"
#include "render/resources/MaterialTable.h"

Falcor::RenderPassReflection DepthPrepass::reflect(const CompileData&)
{
    Falcor::RenderPassReflection reflection;
    reflection.addOutput("depth", "Scene depth")
        .format(rhi::Format::D32Float)
        .bindFlags(Falcor::ResourceBindFlags::DepthStencil);
    return reflection;
}


void DepthPrepass::compile(Falcor::RenderContext* passContext, const CompileData&)
{
    rhi::RenderPipelineDesc desc;
    desc.program      = passContext->shaderManager->program(passContext->shaderManager->getOrLoad("depth"));
    desc.depthStencil = {.format = rhi::Format::D32Float, .depthTestEnable = true};
    pipelines         = createMeshCullPipelines(*passContext->device, desc);
}


void DepthPrepass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    const auto target = renderData.getTexture("depth");
    const auto view   = target->getDefaultView();
    CHECK(view);
    rhi::RenderPassDepthStencilAttachment depth;
    depth.view        = view;
    depth.depthLoadOp = rhi::LoadOp::Clear;
    auto* pass        = renderCommands->commands->beginRenderPass({nullptr, 0, &depth});
    CHECK(pass);
    const auto&      size     = target->getDesc().size;
    const auto       viewport = renderCommands->view->viewport.clamped({size.width, size.height});
    MeshDrawRecorder recorder{*pass, viewport.renderState(), *renderCommands};
    recorder.drawOpaque([&](const auto& material) { return pipelines.get(material); });
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, DepthPrepass>();
}
