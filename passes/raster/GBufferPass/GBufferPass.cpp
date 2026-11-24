#include "passes/raster/GBufferPass/GBufferPass.h"

#include "core/Logger.h"
#include "core/Plugin.h"

#include <slang-rhi.h>
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/resources/MaterialTable.h"


RenderPassReflection GBufferPass::reflect(const CompileData& compileData)
{
    const auto* input  = compileData.connectedResources.getField("depth");
    const auto  extent = input ? glm::uvec2{input->texture().size.width, input->texture().size.height} : glm::uvec2{};

    RenderPassReflection reflection;
    reflection.addInput("depth", rhi::TextureDesc{
        .size = {0, 0, 1},
        .format = rhi::Format::D32Float,
        .usage = rhi::TextureUsage::DepthStencil,
    });
    for (size_t index = 0; index < kColorNames.size(); ++index)
        reflection.addOutput(std::string{kColorNames[index]}, rhi::TextureDesc{
            .size = {extent.x, extent.y, 1},
            .format = kColorFormats[index],
            .usage = rhi::TextureUsage::RenderTarget,
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
        "passes/raster/GBufferPass/gbuffer.slang",
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
    const auto       viewport = mpScene->viewport().clamped({size.width, size.height});
    MeshDrawRecorder recorder{*pass, viewport.renderState(), *mpScene};
    recorder.drawOpaque([&](const auto& material) -> rhi::IShaderObject& { return pipelines.bind(*pass, material); });
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, GBufferPass>();
}
