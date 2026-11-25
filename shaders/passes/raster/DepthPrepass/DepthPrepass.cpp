#include "shaders/passes/raster/DepthPrepass/DepthPrepass.h"

#include <slang-rhi.h>
#include "core/Logger.h"
#include "render/GpuScene.h"
#include "render/plugins/Plugin.h"
#include "render/resources/MaterialTable.h"

const RenderPassReflection::Field DepthPrepass::kPorts[]{
    {
        .name = "depth",
        .resource = rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = rhi::Format::D32Float,
            .usage = rhi::TextureUsage::DepthStencil,
        },
        .visibility = RenderPassReflection::Visibility::Output,
    },
};


void DepthPrepass::compile(RenderContext* passContext, const CompileData&)
{
    rhi::RenderPipelineDesc desc;
    desc.program = passContext->shaderManager->getProgram(
        "shaders/passes/raster/DepthPrepass/depth.slang",
        {"vertMain", "fragMain"},
        {{"ENABLE_CLEARCOAT", "0"}, {"ENABLE_SHEEN", "0"}}
    );
    desc.depthStencil = {.format = rhi::Format::D32Float, .depthTestEnable = true};
    pipelines         = createMeshCullPipelines(*mpDevice, desc);
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
    const ViewportRect viewport{.width = float(size.width), .height = float(size.height)};
    MeshDrawRecorder recorder{*pass, viewport.renderState(), *mpScene};
    recorder.drawOpaque([&](const auto& material) -> rhi::IShaderObject& { return pipelines.bind(*pass, material); });
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, DepthPrepass>();
}
