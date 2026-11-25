#include "shaders/passes/raster/GBufferPass/GBufferPass.h"

#include "core/Logger.h"

#include <slang-rhi.h>
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/plugins/Plugin.h"
#include "render/resources/MaterialTable.h"


RenderPassReflection GBufferPass::reflect(const ResourceCache::DefaultProperties&)
{
    RenderPassReflection reflection;
    reflection.addInput("depth", rhi::TextureDesc{
        .size = {0, 0, 1},
        .format = rhi::Format::D32Float,
        .usage = rhi::TextureUsage::DepthStencil,
    });
    for (size_t index = 0; index < kColorNames.size(); ++index)
        reflection.addField({
            .name = std::string{kColorNames[index]},
            .resource = rhi::TextureDesc{
                .size = {0, 0, 1},
                .format = kColorFormats[index],
                .usage = rhi::TextureUsage::RenderTarget,
            },
            .visibility = RenderPassReflection::Visibility::Output,
            .matchInput = "depth",
            .textureMatch = RenderPassReflection::TextureMatch::Extent,
        });
    return reflection;
}


void GBufferPass::compile(RenderContext* passContext, const CompileData&)
{
    std::array<rhi::ColorTargetDesc, kColorFormats.size()> colors;
    for (size_t index = 0; index < colors.size(); ++index)
        colors[index].format = kColorFormats[index];
    rhi::RenderPipelineDesc desc;
    desc.program = passContext->shaderManager->getProgram(
        "shaders/passes/raster/GBufferPass/gbuffer.slang",
        {"vertMain", "fragMain"},
        {{"ENABLE_CLEARCOAT", "0"}, {"ENABLE_SHEEN", "1"}}
    );
    desc.targets      = colors.data();
    desc.targetCount  = uint32_t(colors.size());
    desc.depthStencil = {
        .format           = rhi::Format::D32Float,
        .depthTestEnable  = true,
        .depthWriteEnable = false,
        .depthFunc        = rhi::ComparisonFunc::Equal
    };
    pipelines = createMeshCullPipelines(*mpDevice, desc);
}


void GBufferPass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    std::array<rhi::RenderPassColorAttachment, kColorFormats.size()> colors;
    std::array<rhi::ComPtr<rhi::ITextureView>, kColorFormats.size()> views;
    for (size_t index = 0; index < colors.size(); ++index)
    {
        views[index] = renderData.getTexture(kColorNames[index])->getDefaultView();
        CHECK(views[index]);
        colors[index].view          = views[index];
        colors[index].clearValue[3] = 1.0f;
    }
    const auto target = renderData.getTexture("depth");
    const auto view   = target->getDefaultView();
    CHECK(view);
    rhi::RenderPassDepthStencilAttachment depth;
    depth.view          = view;
    depth.depthLoadOp   = rhi::LoadOp::Load;
    depth.depthReadOnly = true;
    auto* pass          = renderCommands->commands->beginRenderPass({colors.data(), uint32_t(colors.size()), &depth});
    CHECK(pass);
    const auto&      size     = target->getDesc().size;
    const ViewportRect viewport{.width = float(size.width), .height = float(size.height)};
    MeshDrawRecorder recorder{*pass, viewport.renderState(), *mpScene};
    recorder.drawOpaque([&](const auto& material) -> rhi::IShaderObject& { return pipelines.bind(*pass, material); });
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, GBufferPass>();
}
