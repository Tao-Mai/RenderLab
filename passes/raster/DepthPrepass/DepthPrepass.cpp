#include "passes/raster/DepthPrepass/DepthPrepass.h"

#include <slang-rhi.h>
#include "core/Logger.h"
#include "core/Plugin.h"
#include "render/GpuScene.h"
#include "render/resources/MaterialTable.h"

RenderPassReflection DepthPrepass::reflect(const CompileData&)
{
    RenderPassReflection reflection;
    reflection.addOutput("depth", rhi::TextureDesc{
        .size = {0, 0, 1},
        .format = rhi::Format::D32Float,
        .usage = rhi::TextureUsage::DepthStencil,
    });
    return reflection;
}


void DepthPrepass::compile(RenderContext* passContext, const CompileData&)
{
    rhi::RenderPipelineDesc desc;
    desc.program      = passContext->shaderManager->getOrLoad("depth");
    desc.depthStencil = {.format = rhi::Format::D32Float, .depthTestEnable = true};
    pipelines         = createMeshCullPipelines(*passContext->device, desc);
}


void DepthPrepass::execute(RenderContext* renderCommands, const RenderData& renderData)
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
    const auto       viewport = renderCommands->scene->viewport().clamped({size.width, size.height});
    MeshDrawRecorder recorder{*pass, viewport.renderState(), *renderCommands};
    recorder.drawOpaque([&](const auto& material) -> rhi::IShaderObject& { return pipelines.bind(*pass, material); });
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, DepthPrepass>();
}
